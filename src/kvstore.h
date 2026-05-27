#ifndef KVSTORE_H
#define KVSTORE_H

#include "object.h"
#include <stddef.h>

typedef struct kv_entry {
    char            *key;  // owns
    size_t           klen;
    kv_object       *val;  // owns
    struct kv_entry *next;
} kv_entry;

// size is always power-of-2; enables bitmask indexing
typedef struct {
    kv_entry **buckets;
    size_t     size;
    size_t     count;
    size_t     used_memory; // approximate bytes: entries + keys + object payloads
} kvstore;

// caller must free with kv_destroy()
kvstore *kv_create(void);
void     kv_destroy(kvstore *kv);

// takes ownership of val on success; caller still owns val on failure
int kv_set(kvstore *kv, const char *key, size_t klen, kv_object *val);

// returns borrowed pointer; caller must not free it
kv_object *kv_get(kvstore *kv, const char *key, size_t klen);

// returns 1 if removed, 0 if not present
int kv_del(kvstore *kv, const char *key, size_t klen);

size_t kv_count(const kvstore *kv);

#endif /* KVSTORE_H */
