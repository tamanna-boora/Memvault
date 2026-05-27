#ifndef OBJECT_H
#define OBJECT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    KV_OBJ_STRING,
    KV_OBJ_LIST,
    KV_OBJ_HASH
} kv_obj_type;

// NUL-terminated at buf[len] for debug convenience
typedef struct {
    size_t len;
    char   buf[]; // len+1 bytes allocated
} kv_string;

typedef struct {
    kv_obj_type type;
    void       *ptr;       // owns the payload
    uint64_t    lru;
    int64_t     expire_ms; // 0 means no expiry
} kv_object;

// caller must free with obj_free()
kv_object *obj_create_string(const char *s, size_t len);
kv_object *obj_create_list(void);
kv_object *obj_create_hash(void);

// null-safe
void obj_free(kv_object *o);

// approximate heap bytes for this object and payload
size_t kv_object_size(const kv_object *o);

#endif /* OBJECT_H */
