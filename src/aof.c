#include "aof.h"
#include "server.h"
#include "client.h"
#include "commands.h"
#include "resp.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#  include <io.h>
#  define fileno _fileno
#  define fsync  _commit
#else
#  include <unistd.h>
#endif

#define AOF_PATH     "appendonly.aof"
#define AOF_FSYNC_MS 1000
#define AOF_BUF_INIT 65536

int aof_open(struct kv_server *s) {
    s->aof_fp = fopen(AOF_PATH, "ab");
    if (!s->aof_fp) return -1;
    s->aof_buf = malloc(AOF_BUF_INIT);
    if (!s->aof_buf) { fclose(s->aof_fp); s->aof_fp = NULL; return -1; }
    s->aof_buf_len    = 0;
    s->aof_last_fsync = now_ms();
    return 0;
}

void aof_append(struct kv_server *s, const char *buf, size_t len) {
    if (!s->aof_fp || s->replaying) return;
    if (s->aof_buf_len + len > AOF_BUF_INIT) {
        aof_flush(s);
    }
    if (len > AOF_BUF_INIT) {
        fwrite(buf, 1, len, s->aof_fp);
        return;
    }
    memcpy(s->aof_buf + s->aof_buf_len, buf, len);
    s->aof_buf_len += len;
}

void aof_flush(struct kv_server *s) {
    if (!s->aof_fp) return;
    if (s->aof_buf_len > 0) {
        fwrite(s->aof_buf, 1, s->aof_buf_len, s->aof_fp);
        s->aof_buf_len = 0;
    }
    int64_t now = now_ms();
    if (now - s->aof_last_fsync >= AOF_FSYNC_MS) {
        fflush(s->aof_fp);
        fsync(fileno(s->aof_fp));
        s->aof_last_fsync = now;
    }
}

void aof_close(struct kv_server *s) {
    if (!s->aof_fp) return;
    if (s->aof_buf_len > 0)
        fwrite(s->aof_buf, 1, s->aof_buf_len, s->aof_fp);
    fflush(s->aof_fp);
    fsync(fileno(s->aof_fp));
    fclose(s->aof_fp);
    s->aof_fp = NULL;
    free(s->aof_buf);
    s->aof_buf = NULL;
}

int aof_replay(struct kv_server *s) {
    FILE *f = fopen(AOF_PATH, "rb");
    if (!f) return 0; // no file yet is not an error

    s->replaying = 1;

    // fd=-1 means no real I/O; absorbs reply bytes during replay
    kv_client *dummy = client_create(-1);
    if (!dummy) { fclose(f); s->replaying = 0; return -1; }

    char  *rbuf  = NULL;
    size_t rcap  = 0;
    size_t rused = 0;

    for (;;) {
        if (rused == rcap) {
            size_t new_cap = rcap == 0 ? 4096 : rcap * 2;
            char  *tmp     = realloc(rbuf, new_cap);
            if (!tmp) break;
            rbuf = tmp;
            rcap = new_cap;
        }
        size_t nr = fread(rbuf + rused, 1, rcap - rused, f);
        if (nr == 0) break;
        rused += nr;

        size_t consumed = 0;
        while (consumed < rused) {
            resp_value rv;
            size_t     n  = 0;
            int        rc = resp_parse(rbuf + consumed, rused - consumed, &rv, &n);
            if (rc == 1) break;
            if (rc < 0)  { consumed = rused; break; }
            if (rv.type == RESP_ARRAY && rv.v.array.count > 0) {
                dummy->wused = 0;
                cmd_dispatch(dummy, s, (int)rv.v.array.count, rv.v.array.items,
                             rbuf + consumed, n);
            }
            resp_value_free(&rv);
            consumed += n;
        }
        memmove(rbuf, rbuf + consumed, rused - consumed);
        rused -= consumed;
    }

    free(rbuf);
    client_free(dummy);
    fclose(f);
    s->replaying = 0;
    return 0;
}
