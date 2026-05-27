#include "kvhash.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_BUCKETS 8

// FNV-1a 64-bit hash
static uint64_t fnv1a(const char *key, size_t klen) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < klen; i++) {
        h ^= (uint8_t)key[i];
        h *= 1099511628211ULL;
    }
    return h;
}

kv_hash *kvhash_create(void) {
    kv_hash *h = malloc(sizeof(*h));
    if (!h) return NULL;
    h->buckets = calloc(INITIAL_BUCKETS, sizeof(kv_hentry *));
    if (!h->buckets) { free(h); return NULL; }
    h->size  = INITIAL_BUCKETS;
    h->count = 0;
    return h;
}

static void hentry_free(kv_hentry *e) {
    free(e->field);
    free(e->val);
    free(e);
}

void kvhash_free(kv_hash *h) {
    if (!h) return;
    for (size_t i = 0; i < h->size; i++) {
        kv_hentry *e = h->buckets[i];
        while (e) {
            kv_hentry *next = e->next;
            hentry_free(e);
            e = next;
        }
    }
    free(h->buckets);
    free(h);
}

static int kvhash_resize(kv_hash *h) {
    size_t      new_size = h->size * 2;
    kv_hentry **nb       = calloc(new_size, sizeof(kv_hentry *));
    if (!nb) return -1;
    for (size_t i = 0; i < h->size; i++) {
        kv_hentry *e = h->buckets[i];
        while (e) {
            kv_hentry *next = e->next;
            size_t     idx  = fnv1a(e->field, e->flen) & (new_size - 1);
            e->next  = nb[idx];
            nb[idx]  = e;
            e        = next;
        }
    }
    free(h->buckets);
    h->buckets = nb;
    h->size    = new_size;
    return 0;
}

int kvhash_set(kv_hash *h, const char *field, size_t flen,
               const char *val, size_t vlen) {
    if (h->count >= h->size && kvhash_resize(h) < 0) return -1;

    size_t idx = fnv1a(field, flen) & (h->size - 1);

    for (kv_hentry *e = h->buckets[idx]; e; e = e->next) {
        if (e->flen == flen && memcmp(e->field, field, flen) == 0) {
            kv_string *ns = malloc(sizeof(kv_string) + vlen + 1);
            if (!ns) return -1;
            ns->len = vlen;
            memcpy(ns->buf, val, vlen);
            ns->buf[vlen] = '\0';
            free(e->val);
            e->val = ns;
            return 0; // updated
        }
    }

    kv_hentry *e = malloc(sizeof(kv_hentry));
    if (!e) return -1;
    e->field = malloc(flen);
    e->val   = malloc(sizeof(kv_string) + vlen + 1);
    if (!e->field || !e->val) {
        free(e->field);
        free(e->val);
        free(e);
        return -1;
    }
    memcpy(e->field, field, flen);
    e->flen           = flen;
    e->val->len       = vlen;
    memcpy(e->val->buf, val, vlen);
    e->val->buf[vlen] = '\0';
    e->next           = h->buckets[idx];
    h->buckets[idx]   = e;
    h->count++;
    return 1; // new field
}

kv_string *kvhash_get(kv_hash *h, const char *field, size_t flen) {
    size_t idx = fnv1a(field, flen) & (h->size - 1);
    for (kv_hentry *e = h->buckets[idx]; e; e = e->next) {
        if (e->flen == flen && memcmp(e->field, field, flen) == 0)
            return e->val;
    }
    return NULL;
}

int kvhash_del(kv_hash *h, const char *field, size_t flen) {
    size_t     idx = fnv1a(field, flen) & (h->size - 1);
    kv_hentry **cur = &h->buckets[idx];
    while (*cur) {
        kv_hentry *e = *cur;
        if (e->flen == flen && memcmp(e->field, field, flen) == 0) {
            *cur = e->next;
            hentry_free(e);
            h->count--;
            return 1;
        }
        cur = &e->next;
    }
    return 0;
}
