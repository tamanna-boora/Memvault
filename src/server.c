#include "server.h"
#include "commands.h"
#include "resp.h"
#include "ttl.h"
#include "aof.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
// WSAPoll requires _WIN32_WINNT >= 0x0600 and Ws2_32.lib
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
   typedef SOCKET    sock_t;
   typedef WSAPOLLFD kv_pollfd;
#  define kv_poll    WSAPoll
#  define CLOSESOCK  closesocket
#  define INVALID_FD INVALID_SOCKET
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <unistd.h>
#  include <fcntl.h>
   typedef int            sock_t;
   typedef struct pollfd  kv_pollfd;
#  define kv_poll         poll
#  define CLOSESOCK       close
#  define INVALID_FD      (-1)
#endif

static int set_nonblocking(sock_t fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

kv_server *server_create(int port) {
    kv_server *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->kv = kv_create();
    if (!s->kv) { free(s); return NULL; }
    s->port    = port;
    s->running = 1;

#ifdef _WIN32
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) { kv_destroy(s->kv); free(s); return NULL; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { kv_destroy(s->kv); free(s); return NULL; }
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(fd, 128) < 0) {
        CLOSESOCK(fd);
        kv_destroy(s->kv);
        free(s);
        return NULL;
    }
    set_nonblocking(fd);
    s->listen_fd = (int)fd;
    return s;
}

void server_destroy(kv_server *s) {
    if (!s) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (s->clients[i]) client_free(s->clients[i]);
    }
    aof_close(s);
    kv_destroy(s->kv);
    CLOSESOCK((sock_t)s->listen_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    free(s);
}

static void client_remove(kv_server *s, int slot) {
    kv_client *c = s->clients[slot];
    kv_log("INFO", "client disconnected fd=%d", c->fd);
    CLOSESOCK((sock_t)c->fd);
    client_free(c);
    s->clients[slot] = NULL;
}

static void process_client(kv_server *s, int slot) {
    kv_client *c = s->clients[slot];
    int r = client_read(c);
    if (r == 0 || r < 0) { client_remove(s, slot); return; }

    while (c->rused > 0) {
        resp_value rv;
        size_t     consumed = 0;
        int        pr = resp_parse(c->rbuf, c->rused, &rv, &consumed);
        if (pr == 1) break;
        if (pr < 0)  { client_remove(s, slot); return; }

        if (rv.type == RESP_ARRAY && rv.v.array.count > 0) {
            cmd_dispatch(c, s, (int)rv.v.array.count, rv.v.array.items,
                         c->rbuf, consumed);
        } else {
            const char *msg = "ERR bad request format";
            resp_append_error(&c->wbuf, &c->wused, &c->wcap, msg, strlen(msg));
        }
        resp_value_free(&rv);
        memmove(c->rbuf, c->rbuf + consumed, c->rused - consumed);
        c->rused -= consumed;
    }

    client_write(c);
}

// poll-based, works on Linux, macOS, and Windows
void server_run(kv_server *s) {
    kv_log("INFO", "listening on port %d", s->port);

    kv_pollfd fds[MAX_CLIENTS + 1];

    while (s->running) {
        int nfds = 0;
        fds[nfds].fd      = (sock_t)s->listen_fd;
        fds[nfds].events  = POLLIN;
        fds[nfds].revents = 0;
        nfds++;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!s->clients[i]) continue;
            fds[nfds].fd      = (sock_t)s->clients[i]->fd;
            fds[nfds].events  = POLLIN;
            if (s->clients[i]->wused > 0) fds[nfds].events |= POLLOUT;
            fds[nfds].revents = 0;
            nfds++;
        }

        int ready = kv_poll(fds, (unsigned)nfds, 100);
        if (ready < 0) break;

        // TTL sweep and AOF flush every loop tick (~100ms)
        ttl_sweep(s->kv, 5);
        aof_flush(s);

        for (int i = 0; i < nfds; i++) {
            if (!fds[i].revents) continue;
            if ((int)fds[i].fd == s->listen_fd) {
                struct sockaddr_in ca;
                socklen_t          cl = sizeof(ca);
#ifdef _WIN32
                sock_t cfd = accept((SOCKET)s->listen_fd,
                                    (struct sockaddr *)&ca, &cl);
                if (cfd == INVALID_SOCKET) continue;
#else
                sock_t cfd = accept(s->listen_fd,
                                    (struct sockaddr *)&ca, &cl);
                if (cfd < 0) continue;
#endif
                int slot = -1;
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (!s->clients[j]) { slot = j; break; }
                }
                if (slot < 0) { CLOSESOCK(cfd); continue; }

                set_nonblocking(cfd);
                kv_client *c = client_create((int)cfd);
                if (!c) { CLOSESOCK(cfd); continue; }
                s->clients[slot] = c;
                kv_log("INFO", "client connected fd=%d", (int)cfd);
            } else {
                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (!s->clients[j]) continue;
                    if ((sock_t)s->clients[j]->fd != fds[i].fd) continue;
                    if (fds[i].revents & POLLIN)  process_client(s, j);
                    if (s->clients[j] && (fds[i].revents & POLLOUT))
                        client_write(s->clients[j]);
                    break;
                }
            }
        }
    }
}
