#ifndef CLIENT_H
#define CLIENT_H

#include <stddef.h>

typedef struct {
    int    fd;
    char  *rbuf;
    size_t rused;
    size_t rcap;
    char  *wbuf;
    size_t wused;
    size_t wcap;
} kv_client;

// caller must free with client_free()
kv_client *client_create(int fd);
void       client_free(kv_client *c);

// returns bytes read, 0 on peer close, -1 on error
int client_read(kv_client *c);

// returns bytes written, -1 on error
int client_write(kv_client *c);

// grows wbuf as needed; returns 0 or -1 on OOM
int client_append(kv_client *c, const void *data, size_t n);

#endif /* CLIENT_H */
