#include "commands.h"
#include "server.h"
#include "client.h"
#include "kvstore.h"
#include "object.h"
#include "kvlist.h"
#include "kvhash.h"
#include "resp.h"
#include "aof.h"
#include "eviction.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#ifdef _WIN32
#  define strcasecmp  _stricmp
#  define strncasecmp _strnicmp
#endif

static void reply_ok(kv_client *c) {
    resp_append_simple_string(&c->wbuf, &c->wused, &c->wcap, "OK", 2);
}

static void reply_err(kv_client *c, const char *msg) {
    resp_append_error(&c->wbuf, &c->wused, &c->wcap, msg, strlen(msg));
}

static void reply_int(kv_client *c, int64_t n) {
    resp_append_integer(&c->wbuf, &c->wused, &c->wcap, n);
}

static void reply_bulk(kv_client *c, const char *s, size_t len) {
    resp_append_bulk_string(&c->wbuf, &c->wused, &c->wcap, s, len);
}

static void reply_null(kv_client *c) {
    resp_append_nil(&c->wbuf, &c->wused, &c->wcap);
}

static void reply_wrongtype(kv_client *c) {
    reply_err(c, "WRONGTYPE Operation against a key holding the wrong kind of value");
}

// returns NULL on error (reply already sent)
static kv_list *get_or_create_list(kv_client *c, kv_server *s,
                                    const char *key, size_t klen) {
    kv_object *o = kv_get(s->kv, key, klen);
    if (o) {
        if (o->type != KV_OBJ_LIST) { reply_wrongtype(c); return NULL; }
        return (kv_list *)o->ptr;
    }
    kv_object *no = obj_create_list();
    if (!no) { reply_err(c, "ERR out of memory"); return NULL; }
    if (kv_set(s->kv, key, klen, no) < 0) {
        obj_free(no);
        reply_err(c, "ERR out of memory");
        return NULL;
    }
    return (kv_list *)no->ptr;
}

// returns NULL on error (reply already sent)
static kv_hash *get_or_create_hash(kv_client *c, kv_server *s,
                                    const char *key, size_t klen) {
    kv_object *o = kv_get(s->kv, key, klen);
    if (o) {
        if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return NULL; }
        return (kv_hash *)o->ptr;
    }
    kv_object *no = obj_create_hash();
    if (!no) { reply_err(c, "ERR out of memory"); return NULL; }
    if (kv_set(s->kv, key, klen, no) < 0) {
        obj_free(no);
        reply_err(c, "ERR out of memory");
        return NULL;
    }
    return (kv_hash *)no->ptr;
}

static void cmd_ping(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)s;
    if (argc == 1) {
        resp_append_simple_string(&c->wbuf, &c->wused, &c->wcap, "PONG", 4);
    } else {
        reply_bulk(c, argv[1].v.str.data, argv[1].v.str.len);
    }
}

static void cmd_echo(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)s; (void)argc;
    reply_bulk(c, argv[1].v.str.data, argv[1].v.str.len);
}

static void cmd_set(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    const char *key  = argv[1].v.str.data;
    size_t      klen = argv[1].v.str.len;
    const char *val  = argv[2].v.str.data;
    size_t      vlen = argv[2].v.str.len;

    // evict before writes; evict_if_needed is no-op when maxmemory is 0
    evict_if_needed(s);

    kv_object *o = obj_create_string(val, vlen);
    if (!o) { reply_err(c, "ERR out of memory"); return; }

    if (kv_set(s->kv, key, klen, o) < 0) {
        obj_free(o);
        reply_err(c, "ERR out of memory");
        return;
    }
    reply_ok(c);
}

static void cmd_get(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_null(c); return; }
    if (o->type != KV_OBJ_STRING) { reply_wrongtype(c); return; }
    kv_string *str = (kv_string *)o->ptr;
    reply_bulk(c, str->buf, str->len);
}

static void cmd_del(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    int64_t count = 0;
    for (int i = 1; i < argc; i++)
        count += kv_del(s->kv, argv[i].v.str.data, argv[i].v.str.len);
    reply_int(c, count);
}

static void cmd_exists(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    int64_t count = 0;
    for (int i = 1; i < argc; i++) {
        if (kv_get(s->kv, argv[i].v.str.data, argv[i].v.str.len)) count++;
    }
    reply_int(c, count);
}

static void cmd_expire(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, 0); return; }
    int64_t secs = strtoll(argv[2].v.str.data, NULL, 10);
    o->expire_ms = now_ms() + secs * 1000;
    reply_int(c, 1);
}

static void cmd_ttl(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, -2); return; }
    if (o->expire_ms == 0) { reply_int(c, -1); return; }
    int64_t remaining = (o->expire_ms - now_ms()) / 1000;
    reply_int(c, remaining < 0 ? 0 : remaining);
}

static void cmd_persist(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o || o->expire_ms == 0) { reply_int(c, 0); return; }
    o->expire_ms = 0;
    reply_int(c, 1);
}

static void cmd_lpush(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    evict_if_needed(s);
    kv_list *l = get_or_create_list(c, s, argv[1].v.str.data, argv[1].v.str.len);
    if (!l) return;
    int64_t len = 0;
    for (int i = 2; i < argc; i++)
        len = kvlist_lpush(l, argv[i].v.str.data, argv[i].v.str.len);
    reply_int(c, len);
}

static void cmd_rpush(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    evict_if_needed(s);
    kv_list *l = get_or_create_list(c, s, argv[1].v.str.data, argv[1].v.str.len);
    if (!l) return;
    int64_t len = 0;
    for (int i = 2; i < argc; i++)
        len = kvlist_rpush(l, argv[i].v.str.data, argv[i].v.str.len);
    reply_int(c, len);
}

static void cmd_lpop(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_null(c); return; }
    if (o->type != KV_OBJ_LIST) { reply_wrongtype(c); return; }
    kv_string *val = kvlist_lpop((kv_list *)o->ptr);
    if (!val) { reply_null(c); return; }
    reply_bulk(c, val->buf, val->len);
    free(val);
}

static void cmd_rpop(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_null(c); return; }
    if (o->type != KV_OBJ_LIST) { reply_wrongtype(c); return; }
    kv_string *val = kvlist_rpop((kv_list *)o->ptr);
    if (!val) { reply_null(c); return; }
    reply_bulk(c, val->buf, val->len);
    free(val);
}

static void cmd_llen(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, 0); return; }
    if (o->type != KV_OBJ_LIST) { reply_wrongtype(c); return; }
    reply_int(c, (int64_t)((kv_list *)o->ptr)->len);
}

static void cmd_hset(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    // field-value pairs require even count after key
    if ((argc - 2) % 2 != 0) {
        reply_err(c, "ERR wrong number of arguments for 'hset' command");
        return;
    }
    evict_if_needed(s);
    kv_hash *h = get_or_create_hash(c, s, argv[1].v.str.data, argv[1].v.str.len);
    if (!h) return;
    int64_t added = 0;
    for (int i = 2; i < argc; i += 2) {
        int r = kvhash_set(h, argv[i].v.str.data, argv[i].v.str.len,
                              argv[i+1].v.str.data, argv[i+1].v.str.len);
        if (r == 1) added++;
    }
    reply_int(c, added);
}

static void cmd_hget(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_null(c); return; }
    if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return; }
    kv_string *val = kvhash_get((kv_hash *)o->ptr,
                                argv[2].v.str.data, argv[2].v.str.len);
    if (!val) { reply_null(c); return; }
    reply_bulk(c, val->buf, val->len);
}

static void cmd_hdel(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, 0); return; }
    if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return; }
    int64_t count = 0;
    for (int i = 2; i < argc; i++)
        count += kvhash_del((kv_hash *)o->ptr,
                            argv[i].v.str.data, argv[i].v.str.len);
    reply_int(c, count);
}

static void cmd_hlen(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, 0); return; }
    if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return; }
    reply_int(c, (int64_t)((kv_hash *)o->ptr)->count);
}

static void cmd_hexists(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { reply_int(c, 0); return; }
    if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return; }
    kv_string *val = kvhash_get((kv_hash *)o->ptr,
                                argv[2].v.str.data, argv[2].v.str.len);
    reply_int(c, val ? 1 : 0);
}

static void cmd_lrange(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, 0); return; }
    if (o->type != KV_OBJ_LIST) { reply_wrongtype(c); return; }
    kv_list *l = (kv_list *)o->ptr;
    int64_t len   = (int64_t)l->len;
    int64_t start = strtoll(argv[2].v.str.data, NULL, 10);
    int64_t stop  = strtoll(argv[3].v.str.data, NULL, 10);
    if (start < 0) start = len + start;
    if (stop  < 0) stop  = len + stop;
    if (start < 0) start = 0;
    if (stop >= len) stop = len - 1;
    if (len == 0 || start > stop) {
        resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, 0);
        return;
    }
    int64_t count = stop - start + 1;
    resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, (size_t)count);
    kv_lnode *node = l->head;
    for (int64_t i = 0; i < start; i++) node = node->next;
    for (int64_t i = 0; i < count; i++, node = node->next)
        reply_bulk(c, node->val->buf, node->val->len);
}

static void cmd_flushdb(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc; (void)argv;
    kvstore *fresh = kv_create();
    if (!fresh) { reply_err(c, "ERR out of memory"); return; }
    kv_destroy(s->kv);
    s->kv = fresh;
    reply_ok(c);
}

static void cmd_hgetall(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    (void)argc;
    kv_object *o = kv_get(s->kv, argv[1].v.str.data, argv[1].v.str.len);
    if (!o) { resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, 0); return; }
    if (o->type != KV_OBJ_HASH) { reply_wrongtype(c); return; }
    kv_hash *h = (kv_hash *)o->ptr;
    resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, h->count * 2);
    for (size_t i = 0; i < h->size; i++)
        for (kv_hentry *e = h->buckets[i]; e; e = e->next) {
            reply_bulk(c, e->field, e->flen);
            reply_bulk(c, e->val->buf, e->val->len);
        }
}

// parses "1kb", "2mb", "3gb" and bare digits; returns 0 or -1
static int parse_memory_arg(const char *s, size_t slen, size_t *out) {
    if (slen == 0 || slen >= 64) return -1;
    char buf[64];
    memcpy(buf, s, slen);
    buf[slen] = '\0';

    size_t n   = slen;
    size_t mul = 1;
    if (n >= 2) {
        char last = (char)tolower((unsigned char)buf[n-1]);
        char pre  = (char)tolower((unsigned char)buf[n-2]);
        // allow trailing 'b' (kb/mb/gb)
        if (last == 'b') {
            if      (pre == 'k') { mul = 1024UL;               n -= 2; }
            else if (pre == 'm') { mul = 1024UL * 1024;         n -= 2; }
            else if (pre == 'g') { mul = 1024UL * 1024 * 1024;  n -= 2; }
        }
    }
    buf[n] = '\0';
    char    *end;
    uint64_t v = strtoull(buf, &end, 10);
    if (*end != '\0') return -1;
    *out = (size_t)(v * mul);
    return 0;
}

static void cmd_config(kv_client *c, kv_server *s, int argc, resp_value *argv) {
    if (argc < 3) { reply_err(c, "ERR wrong number of arguments"); return; }
    const char *sub = argv[1].v.str.data;

    if (strcasecmp(sub, "SET") == 0) {
        if (argc < 4) { reply_err(c, "ERR wrong number of arguments"); return; }
        const char *param = argv[2].v.str.data;
        if (strcasecmp(param, "maxmemory") == 0) {
            size_t bytes;
            if (parse_memory_arg(argv[3].v.str.data, argv[3].v.str.len, &bytes) < 0) {
                reply_err(c, "ERR invalid memory value");
                return;
            }
            s->maxmemory = bytes;
            reply_ok(c);
        } else {
            reply_err(c, "ERR unknown config parameter");
        }
    } else if (strcasecmp(sub, "GET") == 0) {
        const char *param = argv[2].v.str.data;
        if (strcasecmp(param, "maxmemory") == 0) {
            char numbuf[32];
            snprintf(numbuf, sizeof(numbuf), "%zu", s->maxmemory);
            resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, 2);
            reply_bulk(c, "maxmemory", 9);
            reply_bulk(c, numbuf, strlen(numbuf));
        } else {
            // unknown param returns empty array (redis compat)
            resp_append_array_header(&c->wbuf, &c->wused, &c->wcap, 0);
        }
    } else {
        reply_err(c, "ERR unknown CONFIG subcommand");
    }
}

static const kv_command cmd_table[] = {
    { "ping",    -1, cmd_ping,    0 },
    { "echo",     2, cmd_echo,    0 },
    { "set",     -3, cmd_set,     1 },
    { "get",      2, cmd_get,     0 },
    { "del",     -2, cmd_del,     1 },
    { "exists",  -2, cmd_exists,  0 },
    { "expire",   3, cmd_expire,  1 },
    { "ttl",      2, cmd_ttl,     0 },
    { "persist",  2, cmd_persist, 1 },
    // List
    { "lpush",   -3, cmd_lpush,   1 },
    { "rpush",   -3, cmd_rpush,   1 },
    { "lpop",     2, cmd_lpop,    1 },
    { "rpop",     2, cmd_rpop,    1 },
    { "llen",     2, cmd_llen,    0 },
    { "lrange",   4, cmd_lrange,  0 },
    // Db
    { "flushdb",  1, cmd_flushdb, 1 },
    // Hash
    { "hset",    -4, cmd_hset,    1 },
    { "hget",     3, cmd_hget,    0 },
    { "hdel",    -3, cmd_hdel,    1 },
    { "hlen",     2, cmd_hlen,    0 },
    { "hexists",  3, cmd_hexists, 0 },
    { "hgetall",  2, cmd_hgetall, 0 },
    // Config
    { "config",  -3, cmd_config,  0 },
};

#define CMD_TABLE_LEN (sizeof(cmd_table) / sizeof(cmd_table[0]))

void cmd_dispatch(kv_client *c, kv_server *s, int argc, resp_value *argv,
                  const char *raw, size_t raw_len) {
    if (argc == 0) return;
    const char *name = argv[0].v.str.data;
    for (size_t i = 0; i < CMD_TABLE_LEN; i++) {
        const kv_command *cmd = &cmd_table[i];
        if (strcasecmp(name, cmd->name) != 0) continue;
        int ok = (cmd->arity > 0) ? (argc == cmd->arity) : (argc >= -cmd->arity);
        if (!ok) {
            reply_err(c, "ERR wrong number of arguments");
            return;
        }
        cmd->handler(c, s, argc, argv);
        // skip during replay to avoid double-logging
        if (cmd->is_write && !s->replaying)
            aof_append(s, raw, raw_len);
        return;
    }
    reply_err(c, "ERR unknown command");
}
