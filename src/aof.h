#ifndef AOF_H
#define AOF_H

#include <stddef.h>

struct kv_server; // forward declaration

// opens or creates the AOF file for appending; returns 0 or -1
int  aof_open(struct kv_server *s);

// buffers raw RESP bytes; no-op when AOF is not open
void aof_append(struct kv_server *s, const char *buf, size_t len);

// flush to OS and fsync if due; call once per event loop tick
void aof_flush(struct kv_server *s);

// flushes and fsyncs before closing
void aof_close(struct kv_server *s);

// sets s->replaying to suppress aof_append during replay; returns 0 if no file yet
int  aof_replay(struct kv_server *s);

#endif /* AOF_H */
