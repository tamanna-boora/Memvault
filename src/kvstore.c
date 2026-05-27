#include "kvstore.h"
#include "object.h"
#include "util.h"

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

// overhead per entry beyond the object payload
static size_t entry_overhead(size_t klen) {
    return sizeof(kv_entry) + klen;
}

kvstore *kv_create(void) {
    kvstore *kv = malloc(sizeof(*kv));
    if (!kv) return NULL;
    kv->buckets = calloc(INITIAL_BUCKETS, sizeof(kv_entry *));
    if (!kv->buckets) { free(kv); return NULL; }
    kv->size        = INITIAL_BUCKETS;
    kv->count       = 0;
    kv->used_memory = 0;
    return kv;
}

void kv_destroy(kvstore *kv) {
    if (!kv) return;
    for (size_t i = 0; i < kv->size; i++) {
        kv_entry *e = kv->buckets[i];
        while (e) {
            kv_entry *next = e->next;
            free(e->key);
            obj_free(e->val);
            free(e);
            e = next;
        }
    }
    free(kv->buckets);
    free(kv);
}

// stop-the-world resize; doubles bucket count
static int kv_resize(kvstore *kv) {
    size_t     new_size = kv->size * 2;
    kv_entry **nb       = calloc(new_size, sizeof(kv_entry *));
    if (!nb) return -1;
    for (size_t i = 0; i < kv->size; i++) {
        kv_entry *e = kv->buckets[i];
        while (e) {
            kv_entry *next = e->next;
            size_t    idx  = fnv1a(e->key, e->klen) & (new_size - 1);
            e->next       = nb[idx];
            nb[idx]       = e;
            e             = next;
        }
    }
    free(kv->buckets);
    kv->buckets = nb;
    kv->size    = new_size;
    return 0;
}

int kv_set(kvstore *kv, const char *key, size_t klen, kv_object *val) {
    if (kv->count >= kv->size && kv_resize(kv) < 0) return -1;

    size_t idx = fnv1a(key, klen) & (kv->size - 1);

    val->lru = (uint64_t)now_ms(); // stamp on write

    for (kv_entry *e = kv->buckets[idx]; e; e = e->next) {
        if (e->klen == klen && memcmp(e->key, key, klen) == 0) {
            size_t old_sz = kv_object_size(e->val);
            obj_free(e->val);
            e->val = val;
            size_t new_sz = kv_object_size(val);
            // guard against underflow on approximate accounting
            if (old_sz <= kv->used_memory) kv->used_memory -= old_sz;
            else                            kv->used_memory  = 0;
            kv->used_memory += new_sz;
            return 0;
        }
    }

    kv_entry *e = malloc(sizeof(kv_entry));
    if (!e) return -1;
    e->key = malloc(klen);
    if (!e->key) { free(e); return -1; }
    memcpy(e->key, key, klen);
    e->klen          = klen;
    e->val           = val;
    e->next          = kv->buckets[idx];
    kv->buckets[idx] = e;
    kv->count++;
    kv->used_memory += entry_overhead(klen) + kv_object_size(val);
    return 0;
}

kv_object *kv_get(kvstore *kv, const char *key, size_t klen) {
    size_t idx = fnv1a(key, klen) & (kv->size - 1);
    for (kv_entry *e = kv->buckets[idx]; e; e = e->next) {
        if (e->klen == klen && memcmp(e->key, key, klen) == 0) {
            // passive expiry check
            if (e->val->expire_ms != 0 && now_ms() >= e->val->expire_ms) {
                kv_del(kv, key, klen);
                return NULL;
            }
            e->val->lru = (uint64_t)now_ms(); // stamp on read
            return e->val;
        }
    }
    return NULL;
}

int kv_del(kvstore *kv, const char *key, size_t klen) {
    size_t     idx = fnv1a(key, klen) & (kv->size - 1);
    kv_entry **cur = &kv->buckets[idx];
    while (*cur) {
        kv_entry *e = *cur;
        if (e->klen == klen && memcmp(e->key, key, klen) == 0) {
            *cur = e->next;
            size_t sz = entry_overhead(klen) + kv_object_size(e->val);
            if (sz <= kv->used_memory) kv->used_memory -= sz;
            else                        kv->used_memory  = 0;
            free(e->key);
            obj_free(e->val);
            free(e);
            kv->count--;
            return 1;
        }
        cur = &e->next;
    }
    return 0;
}

size_t kv_count(const kvstore *kv) {
    return kv->count;
}
