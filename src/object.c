#include "object.h"
#include "kvlist.h"
#include "kvhash.h"

#include <stdlib.h>
#include <string.h>

kv_object *obj_create_string(const char *s, size_t len) {
    kv_string *ks = malloc(sizeof(kv_string) + len + 1);
    if (!ks) return NULL;
    ks->len = len;
    memcpy(ks->buf, s, len);
    ks->buf[len] = '\0';

    kv_object *o = malloc(sizeof(kv_object));
    if (!o) { free(ks); return NULL; }
    o->type      = KV_OBJ_STRING;
    o->ptr       = ks;
    o->lru       = 0;
    o->expire_ms = 0;
    return o;
}

kv_object *obj_create_list(void) {
    kv_list *l = kvlist_create();
    if (!l) return NULL;
    kv_object *o = malloc(sizeof(kv_object));
    if (!o) { kvlist_free(l); return NULL; }
    o->type      = KV_OBJ_LIST;
    o->ptr       = l;
    o->lru       = 0;
    o->expire_ms = 0;
    return o;
}

kv_object *obj_create_hash(void) {
    kv_hash *h = kvhash_create();
    if (!h) return NULL;
    kv_object *o = malloc(sizeof(kv_object));
    if (!o) { kvhash_free(h); return NULL; }
    o->type      = KV_OBJ_HASH;
    o->ptr       = h;
    o->lru       = 0;
    o->expire_ms = 0;
    return o;
}

size_t kv_object_size(const kv_object *o) {
    if (!o) return 0;
    size_t sz = sizeof(kv_object);
    switch (o->type) {
    case KV_OBJ_STRING: {
        const kv_string *s = (const kv_string *)o->ptr;
        sz += sizeof(kv_string) + s->len + 1;
        break;
    }
    case KV_OBJ_LIST: {
        const kv_list *l = (const kv_list *)o->ptr;
        sz += sizeof(kv_list);
        for (const kv_lnode *n = l->head; n; n = n->next)
            sz += sizeof(kv_lnode) + sizeof(kv_string) + n->val->len + 1;
        break;
    }
    case KV_OBJ_HASH: {
        const kv_hash *h = (const kv_hash *)o->ptr;
        sz += sizeof(kv_hash) + h->size * sizeof(kv_hentry *);
        for (size_t i = 0; i < h->size; i++)
            for (const kv_hentry *e = h->buckets[i]; e; e = e->next)
                sz += sizeof(kv_hentry) + e->flen + sizeof(kv_string) + e->val->len + 1;
        break;
    }
    }
    return sz;
}

void obj_free(kv_object *o) {
    if (!o) return;
    switch (o->type) {
        case KV_OBJ_STRING:
            free(o->ptr);
            break;
        case KV_OBJ_LIST:
            kvlist_free((kv_list *)o->ptr);
            break;
        case KV_OBJ_HASH:
            kvhash_free((kv_hash *)o->ptr);
            break;
    }
    free(o);
}
