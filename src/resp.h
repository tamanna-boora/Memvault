#ifndef RESP_H
#define RESP_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    RESP_SIMPLE_STRING,
    RESP_ERROR,
    RESP_INTEGER,
    RESP_BULK_STRING,
    RESP_ARRAY,
    RESP_NIL
} resp_type;

typedef struct resp_value {
    resp_type type;
    union {
        struct { char *data; size_t len; } str; // SIMPLE_STRING, ERROR, BULK_STRING
        int64_t integer;                         // INTEGER
        struct { struct resp_value *items; size_t count; } array; // ARRAY
    } v;
} resp_value;

// returns 0 (ok, *consumed set), 1 (need more data), -1 (protocol error)
int  resp_parse(const char *buf, size_t buflen, resp_value *out, size_t *consumed);
void resp_value_free(resp_value *v);

// serialisers: append to (buf, used, cap); return 0 or -1 on OOM
int resp_append_simple_string(char **buf, size_t *used, size_t *cap, const char *s, size_t len);
int resp_append_error(char **buf, size_t *used, size_t *cap, const char *s, size_t len);
int resp_append_integer(char **buf, size_t *used, size_t *cap, int64_t n);
int resp_append_bulk_string(char **buf, size_t *used, size_t *cap, const char *s, size_t len);
int resp_append_nil(char **buf, size_t *used, size_t *cap);
int resp_append_array_header(char **buf, size_t *used, size_t *cap, size_t count);

#endif /* RESP_H */
