#include "ttl.h"
#include "util.h"
#include "object.h"

#include <stdlib.h>

// cap per-sweep to bound latency
#define SWEEP_MAX_ENTRIES 100

void ttl_sweep(kvstore *kv, int64_t budget_ms) {
    int64_t deadline = now_ms() + budget_ms;
    size_t  scanned  = 0;

    // random start so we don't always scan the same prefix
    size_t start = (size_t)((uint64_t)now_ms() & (kv->size - 1));

    for (size_t i = 0; i < kv->size && scanned < SWEEP_MAX_ENTRIES; i++) {
        size_t     idx = (start + i) & (kv->size - 1);
        kv_entry **cur = &kv->buckets[idx];
        while (*cur) {
            kv_entry *e = *cur;
            if (e->val->expire_ms != 0 && now_ms() >= e->val->expire_ms) {
                // unlink directly to avoid a second bucket walk
                *cur = e->next;
                size_t sz = sizeof(kv_entry) + e->klen + kv_object_size(e->val);
                if (sz <= kv->used_memory) kv->used_memory -= sz;
                else                        kv->used_memory  = 0;
                free(e->key);
                obj_free(e->val);
                free(e);
                kv->count--;
            } else {
                cur = &e->next;
            }
            scanned++;
            if (scanned >= SWEEP_MAX_ENTRIES) break;
        }
        if (now_ms() >= deadline) break;
    }
}
