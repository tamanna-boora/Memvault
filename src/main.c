#include "server.h"
#include "aof.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define DEFAULT_PORT 6380

static kv_server *g_server = NULL;

static void handle_signal(int sig) {
    (void)sig;
    if (g_server) g_server->running = 0;
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "usage: kvault [--port <port>]\n");
            return 0;
        }
    }

    kv_server *s = server_create(port);
    if (!s) {
        kv_log("ERROR", "failed to create server on port %d", port);
        return 1;
    }
    g_server = s;

    signal(SIGINT,  handle_signal);
    signal(SIGTERM, handle_signal);

    if (aof_open(s) < 0) {
        kv_log("WARN", "could not open AOF file");
    } else {
        if (aof_replay(s) < 0) {
            kv_log("WARN", "AOF replay failed");
        }
    }

    server_run(s);
    server_destroy(s);
    return 0;
}
