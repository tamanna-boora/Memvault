#ifndef COMMANDS_H
#define COMMANDS_H

#include "client.h"
#include "resp.h"
#include <stddef.h>

struct kv_server;

typedef void (*kv_cmd_fn)(kv_client *c, struct kv_server *s,
                           int argc, resp_value *argv);

typedef struct {
    const char *name;
    int         arity;   /* exact argc if >0; -N means >= N */
    kv_cmd_fn   handler;
    int         is_write;
} kv_command;

void cmd_dispatch(kv_client *c, struct kv_server *s, int argc, resp_value *argv,
                  const char *raw, size_t raw_len);

#endif /* COMMANDS_H */
