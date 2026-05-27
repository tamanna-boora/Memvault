#ifndef SERVER_H
#define SERVER_H

#include "kvstore.h"
#include "client.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_CLIENTS 1024

typedef struct kv_server {
    int        listen_fd;
    int        port;
    kvstore   *kv;
    kv_client *clients[MAX_CLIENTS]; // indexed by fd; NULL if unused
    int        running;
    int        replaying; // set during AOF replay to suppress aof_append
    size_t     maxmemory; // 0 means unlimited
    FILE      *aof_fp;
    char      *aof_buf;
    size_t     aof_buf_len;
    int64_t    aof_last_fsync;
} kv_server;

// caller must free with server_destroy()
kv_server *server_create(int port);
void       server_destroy(kv_server *s);

void server_run(kv_server *s);

#endif /* SERVER_H */
