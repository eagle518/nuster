/*
 * Cache parser related variables and functions.
 *
 * Copyright (C) 2017, [Jiang Wenyuan](https://github.com/jiangwenyuan), < koubunen AT gmail DOT com >
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <ctype.h>

#include <common/cfgparse.h>

#include <types/proxy.h>
#include <types/cache.h>

#include <proto/acl.h>
#include <proto/cache.h>

static const char *cache_id = "cache filter";

static struct cache_key *_cache_parse_rule_key(char *str) {
    struct cache_key *key = NULL;
    if(!strcmp(str, "method")) {
        key = malloc(sizeof(*key));
        key->type = CK_METHOD;
        key->data = NULL;
    } else if(!strcmp(str, "scheme")) {
        key = malloc(sizeof(*key));
        key->type = CK_SCHEME;
        key->data = NULL;
    } else if(!strcmp(str, "host")) {
        key = malloc(sizeof(*key));
        key->type = CK_HOST;
        key->data = NULL;
    } else if(!strcmp(str, "path")) {
        key = malloc(sizeof(*key));
        key->type = CK_PATH;
        key->data = NULL;
    } else if(!strcmp(str, "query")) {
        key = malloc(sizeof(*key));
        key->type = CK_QUERY;
        key->data = NULL;
    } else if(!strncmp(str, "param_", 6) && strlen(str) > 6) {
        key = malloc(sizeof(*key));
        key->type = CK_PARAM;
        key->data = strdup(str + 6);
    } else if(!strncmp(str, "header_", 7) && strlen(str) > 7) {
        key = malloc(sizeof(*key));
        key->type = CK_HEADER;
        key->data = strdup(str + 7);
    } else if(!strncmp(str, "cookie_", 7) && strlen(str) > 7) {
        key = malloc(sizeof(*key));
        key->type = CK_COOKIE;
        key->data = strdup(str + 7);
    } else if(!strcmp(str, "body")) {
        key = malloc(sizeof(*key));
        key->type = CK_BODY;
        key->data = NULL;
    }
    return key;
}

static struct cache_key **cache_parse_rule_key(char *str) {
    struct cache_key **pk = NULL;
    char *m, *tmp = strdup(str);
    int i = 0;

    m = strtok(tmp, ".");
    while(m) {
        struct cache_key *key = _cache_parse_rule_key(m);
        if(!key) {
            goto err;
        }
        pk = realloc(pk, (i + 1) * sizeof(struct cache_key *));
        pk[i++] = key;
        m = strtok(NULL, ".");
    }
    if(!pk) {
        goto err;
    }
    pk = realloc(pk, (i + 1) * sizeof(struct cache_key *));
    pk[i] = NULL;
    free(tmp);
    return pk;
err:
    if(pk) {
        while(i--) {
            free(pk[i]);
        }
        free(pk);
    }
    free(tmp);
    return NULL;
}

static struct cache_code *cache_parse_rule_code(char *str) {
    if(!strcmp(str, "all")) {
        return NULL;
    } else {
        struct cache_code *code = NULL;
        char *tmp = strdup(str);
        char *m = strtok(tmp, ",");
        /* warn ","? */
        while(m) {
            int i = atoi(m);
            struct cache_code *cc = malloc(sizeof(*cc));
            cc->code = i;
            if(code) {
                cc->next = code;
            } else {
                cc->next = NULL;
            }
            code = cc;
            m = strtok(NULL, ",");
        }
        free(tmp);
        return code;
    }
}

int cache_parse_rule(char **args, int section, struct proxy *proxy,
        struct proxy *defpx, const char *file, int line, char **err) {

    struct cache_rule *rule;
    struct acl_cond *cond = NULL;
    char *name = NULL;
    char *key = NULL;
    char *code = NULL;
    unsigned ttl = CACHE_DEFAULT_TTL;
    int cur_arg = 1;

    if(proxy == defpx || !(proxy->cap & PR_CAP_BE)) {
        memprintf(err, "`cache-rule` is not allowed in a 'frontend' or 'defaults' section.");
        return -1;
    }

    if(*(args[cur_arg]) == 0) {
        memprintf(err, "'%s' expects a name.", args[0]);
        return -1;
    }

    name = strdup(args[cur_arg]);
    cur_arg = 2;
    while(*(args[cur_arg]) !=0 && strcmp(args[cur_arg], "if") !=0 && strcmp(args[cur_arg], "unless") != 0) {
        if(!strcmp(args[cur_arg], "key")) {
            if(key != NULL) {
                memprintf(err, "'%s %s': key already specified.", args[0], name);
                goto out;
            }
            cur_arg++;
            if(*(args[cur_arg]) == 0) {
                memprintf(err, "'%s %s': expects a key.", args[0], name);
                goto out;
            }
            key = args[cur_arg];
            cur_arg++;
            continue;
        }
        if(!strcmp(args[cur_arg], "ttl")) {
            const char *res = NULL;
            if((key == NULL && cur_arg >= 4) || (key !=NULL && cur_arg >= 6)) {
                memprintf(err, "'%s %s': ttl already specified.", args[0], name);
                goto out;
            }
            cur_arg++;
            if(*args[cur_arg] == 0) {
                memprintf(err, "'%s %s': expects a ttl(in seconds).", args[0], name);
                goto out;
            }
            /* "d", "h", "m", "s"
             * s is returned, 1 will be returned if ttl less than 1s
             * */
            res = parse_time_err(args[cur_arg], &ttl, TIME_UNIT_S);
            if(res) {
                memprintf(err, "'%s %s': invalid ttl.", args[0], name);
                goto out;
            }
            cur_arg++;
            continue;
        }
        if(!strcmp(args[cur_arg], "code")) {
            if(key != NULL) {
                memprintf(err, "'%s %s': code already specified.", args[0], name);
                goto out;
            }
            cur_arg++;
            if(*(args[cur_arg]) == 0) {
                memprintf(err, "'%s %s': expects a code.", args[0], name);
                goto out;
            }
            code = args[cur_arg];
            cur_arg++;
            continue;
        }
        memprintf(err, "'%s %s': Unrecognized '%s'.", args[0], name, args[cur_arg]);
        goto out;
    }

    if(!strcmp(args[cur_arg], "if") || !strcmp(args[cur_arg], "unless")) {
        if(*args[cur_arg + 1] != 0) {
            char *errmsg = NULL;
            if((cond = build_acl_cond(file, line, proxy, (const char **)args + cur_arg, &errmsg)) == NULL) {
                memprintf(err, "%s", errmsg);
                free(errmsg);
                goto out;
            }
        } else {
            memprintf(err, "'%s %s': [if|unless] expects an acl.", args[0], name);
            goto out;
        }
    }

    rule = malloc(sizeof(*rule));
    rule->cond = cond;
    rule->name = name;
    rule->key = cache_parse_rule_key(key == NULL ? CACHE_DEFAULT_KEY : key);
    if(!rule->key) {
        memprintf(err, "'%s %s': invalid key.", args[0], name);
        goto out;
    }
    rule->code = cache_parse_rule_code(code == NULL ? CACHE_DEFAULT_CODE : code);
    rule->ttl = ttl;
    LIST_INIT(&rule->list);
    LIST_ADDQ(&proxy->cache_rules, &rule->list);

    return 0;
out:
    return -1;
}

int cache_parse_filter(char **args, int *cur_arg, struct proxy *px,
        struct flt_conf *fconf, char **err, void *private) {

    struct flt_conf *fc, *back;
    struct cache_config *conf;

    if(!(px->cap & PR_CAP_BE)) {
        memprintf(err, "`cache` is not allowed in a 'frontend' section.");
        return -1;
    }

    conf = malloc(sizeof(*conf));
    memset(conf, 0, sizeof(*conf));
    if(!conf) {
        memprintf(err, "%s: out of memory", args[*cur_arg]);
        return -1;
    }
    list_for_each_entry_safe(fc, back, &px->filter_configs, list) {
        if(fc->id == cache_id) {
            memprintf(err, "%s: Proxy supports only one cache filter\n", px->id);
            return -1;
        }
    }

    conf->status = CACHE_STATUS_ON;
    (*cur_arg)++;
    if(*args[*cur_arg]) {
        if(!strcmp(args[*cur_arg], "off")) {
            conf->status = CACHE_STATUS_OFF;
        } else if(!strcmp(args[*cur_arg], "on")) {
            conf->status = CACHE_STATUS_ON;
        } else {
            memprintf(err, "%s: expects [on|off], default on", args[*cur_arg]);
            return -1;
        }
        (*cur_arg)++;
    }

    fconf->id   = cache_id;
    fconf->conf = conf;
    fconf->ops  = &cache_filter_ops;
    return 0;

}

/*
 * Parse size
 */
const char *cache_parse_size(const char *text, uint64_t *ret) {
    uint64_t value = 0;

    while(1) {
        unsigned int i;
        i = *text - '0';
        if(i > 9)
            break;
        if(value > ~0ULL / 10)
            goto end;
        value *= 10;
        if(value > (value + i))
            goto end;
        value += i;
        text++;
    }

    switch(*text) {
        case '\0':
            break;
        case 'M':
        case 'm':
            if(value > ~0ULL >> 20)
                goto end;
            value = value << 20;
            break;
        case 'G':
        case 'g':
            if(value > ~0ULL >> 30)
                goto end;
            value = value << 30;
            break;
        default:
            return text;
    }

    if(*text != '\0' && *++text != '\0')
        return text;

    if(value < CACHE_DEFAULT_SIZE) 
        value = CACHE_DEFAULT_SIZE;
    *ret = value;
    return NULL;
end:
    *ret = CACHE_DEFAULT_SIZE;
    return NULL;
}

