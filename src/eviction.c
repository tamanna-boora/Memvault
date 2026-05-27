#include "eviction.h"
#include "server.h"
#include "kvstore.h"
#include "object.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define EVICT_SAMPLES 5

void evict_if_needed(struct kv_server *s) {
    if (s->maxmemory == 0 || s->kv->used_memory <= s->maxmemory) return;

    while (s->kv->used_memory > s->maxmemory && s->kv->count > 0) {
        kv_entry *coldest     = NULL;
        uint64_t  coldest_lru = UINT64_MAX;

        // sample EVICT_SAMPLES non-empty buckets, track globally coldest
        for (int sample = 0; sample < EVICT_SAMPLES; sample++) {
            size_t start = (size_t)rand() % s->kv->size;
            for (size_t j = 0; j < s->kv->size; j++) {
                size_t    idx = (start + j) & (s->kv->size - 1);
                kv_entry *e   = s->kv->buckets[idx];
                if (e) {
                    if (e->val->lru < coldest_lru) {
                        coldest_lru = e->val->lru;
                        coldest     = e;
                    }
                    break; // one non-empty bucket per sample
                }
            }
        }

        if (!coldest) break;

        // kv_del re-walks the bucket so we need a stable key copy
        size_t klen = coldest->klen;
        char  *key  = malloc(klen);
        if (!key) break;
        memcpy(key, coldest->key, klen);
        kv_del(s->kv, key, klen);
        free(key);
    }
}
