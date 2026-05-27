#include "client.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
#  include <winsock2.h>
#  define sock_recv(fd, buf, len) recv((SOCKET)(fd), (buf), (int)(len), 0)
#  define sock_send(fd, buf, len) send((SOCKET)(fd), (buf), (int)(len), 0)
#  define SOCK_EAGAIN (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#  include <unistd.h>
#  define sock_recv(fd, buf, len) read((fd), (buf), (len))
#  define sock_send(fd, buf, len) write((fd), (buf), (len))
#  define SOCK_EAGAIN (errno == EAGAIN || errno == EWOULDBLOCK)
#endif

#define RBUF_INIT 4096
#define WBUF_INIT 4096

kv_client *client_create(int fd) {
    kv_client *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->fd   = fd;
    c->rbuf = malloc(RBUF_INIT);
    c->wbuf = malloc(WBUF_INIT);
    if (!c->rbuf || !c->wbuf) {
        free(c->rbuf);
        free(c->wbuf);
        free(c);
        return NULL;
    }
    c->rcap = RBUF_INIT;
    c->wcap = WBUF_INIT;
    return c;
}

void client_free(kv_client *c) {
    if (!c) return;
    free(c->rbuf);
    free(c->wbuf);
    free(c);
}

int client_read(kv_client *c) {
    int total = 0;
    for (;;) {
        if (c->rcap - c->rused < 512) {
            size_t newcap = c->rcap * 2;
            char  *nb    = realloc(c->rbuf, newcap);
            if (!nb) return -1;
            c->rbuf = nb;
            c->rcap = newcap;
        }
        int n = (int)sock_recv(c->fd, c->rbuf + c->rused, c->rcap - c->rused);
        if (n < 0) {
            if (SOCK_EAGAIN) break;
            return -1;
        }
        if (n == 0) return total == 0 ? 0 : total;
        c->rused += (size_t)n;
        total    += n;
    }
    return total;
}

int client_write(kv_client *c) {
    int total = 0;
    while (c->wused > 0) {
        int n = (int)sock_send(c->fd, c->wbuf, c->wused);
        if (n < 0) {
            if (SOCK_EAGAIN) break;
            return -1;
        }
        c->wused -= (size_t)n;
        if (c->wused > 0)
            memmove(c->wbuf, c->wbuf + n, c->wused);
        total += n;
    }
    return total;
}

int client_append(kv_client *c, const void *data, size_t n) {
    if (c->wcap - c->wused < n) {
        size_t newcap = c->wcap;
        while (newcap - c->wused < n) newcap *= 2;
        char *nb = realloc(c->wbuf, newcap);
        if (!nb) return -1;
        c->wbuf = nb;
        c->wcap = newcap;
    }
    memcpy(c->wbuf + c->wused, data, n);
    c->wused += n;
    return 0;
}
