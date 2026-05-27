#ifndef KVLIST_H
#define KVLIST_H

#include "object.h"
#include <stddef.h>
#include <stdint.h>

typedef struct kv_lnode {
    kv_string       *val;  // owns the string
    struct kv_lnode *prev;
    struct kv_lnode *next;
} kv_lnode;

typedef struct {
    kv_lnode *head;
    kv_lnode *tail;
    size_t    len;
} kv_list;

// caller must free with kvlist_free()
kv_list *kvlist_create(void);
void     kvlist_free(kv_list *l);

// returns new length, or -1 on OOM
int64_t kvlist_lpush(kv_list *l, const char *val, size_t vlen);
int64_t kvlist_rpush(kv_list *l, const char *val, size_t vlen);

// returns owned kv_string (caller must free), or NULL if empty
kv_string *kvlist_lpop(kv_list *l);
kv_string *kvlist_rpop(kv_list *l);

#endif /* KVLIST_H */
