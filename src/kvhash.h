#ifndef KVHASH_H
#define KVHASH_H

#include "object.h"
#include <stddef.h>

typedef struct kv_hentry {
    char             *field; // owns
    size_t            flen;
    kv_string        *val;   // owns
    struct kv_hentry *next;
} kv_hentry;

// size is always power-of-2; enables bitmask indexing
typedef struct {
    kv_hentry **buckets;
    size_t      size;
    size_t      count;
} kv_hash;

// caller must free with kvhash_free()
kv_hash *kvhash_create(void);
void     kvhash_free(kv_hash *h);

// returns 1 if new field, 0 if updated, -1 on OOM
int kvhash_set(kv_hash *h, const char *field, size_t flen,
               const char *val, size_t vlen);

// returns borrowed pointer; never free it
kv_string *kvhash_get(kv_hash *h, const char *field, size_t flen);

// returns 1 if removed, 0 if not present
int kvhash_del(kv_hash *h, const char *field, size_t flen);

#endif /* KVHASH_H */
