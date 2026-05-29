# Memvault — Architecture

A Redis-inspired, single-threaded, in-memory key-value store built from scratch in C11.
Speaks RESP2 — so `redis-cli` works as the client out of the box.

---

## What it is

Memvault is a TCP server that holds data in RAM and speaks the same wire protocol as Redis.
You connect with `redis-cli`, send commands like `SET foo bar`, and get back `+OK`.
When you restart the server, AOF replay restores the database from the command log on disk.

---

## Design goals

- Single-threaded event loop — no locks, no races, everything serialized
- Zero third-party dependencies — just libc and POSIX
- Binary-safe keys and values — NUL bytes are valid anywhere
- Survives `kill -9` — AOF persistence replays on startup
- Compatible with `redis-cli` — RESP2 protocol throughout

---

## Request lifecycle

```
redis-cli                     Memvault process                        Disk
─────────                    ──────────────────────────────          ──────
SET foo bar  ──── TCP ──▶    1. TCP server (event loop)
                                  accept(), read into rbuf
                             2. RESP parser
                                  "*3\r\n$3\r\nSET\r\n..."
                                  → argv = ["SET", "foo", "bar"]
                             3. Command dispatcher
                                  lookup "SET" in command table
                                  arity check (needs 3 args ✓)
                             4. SET handler
                                  obj = obj_create_string("bar", 3)
                                  kv_set(store, "foo", 3, obj)
                                  stamp obj->lru = now
                             5. AOF                   ── append ──▶  memvault.aof
                                  write raw RESP bytes to log
+OK          ◀─── TCP ───    6. Reply
                                  "+OK\r\n" → wbuf → socket
```

---

## File layout

```
memvault/
├── ARCHITECTURE.md   this file
├── Makefile          build system
├── README.md         project overview
├── .gitignore        excludes binaries, .o files, .aof files
└── src/
    ├── main.c        entry point: arg parse, AOF replay, server start
    ├── server.c/h    event loop (epoll/kqueue), listener, client tracking
    ├── client.c/h    per-connection state: fd, read/write ring buffers
    ├── resp.c/h      incremental RESP2 parser + serializer
    ├── commands.c/h  command table and handler dispatch
    ├── kvstore.c/h   main hash table
    ├── object.c/h    value wrapper: type tag, payload ptr, LRU clock, TTL
    ├── kvlist.c/h    doubly linked list (LIST type payload)
    ├── kvhash.c/h    nested hash table (HASH type payload)
    ├── ttl.c/h       expiry tracking and active background sweeper
    ├── eviction.c/h  LRU eviction policy
    ├── aof.c/h       append-only file: write + replay on startup
    └── util.c/h      logging, time helpers
```

---

## Core modules

### kvstore — the database

A chained hash table. Every key maps to a `kv_object`.

```
kvstore
├── buckets[]         array of bucket heads, size always power-of-2
├── size              current bucket count
└── count             number of live keys

kv_object
├── type              KV_OBJ_STRING | KV_OBJ_LIST | KV_OBJ_HASH
├── ptr               points to kv_string / kv_list / kv_hash payload
├── lru               monotonic clock stamp, updated on every access
└── expire_ms         absolute expiry timestamp, -1 = no expiry
```

Hash function: FNV-1a 64-bit. Bucket index: `hash & (size - 1)` — power-of-2
lets us use bitmask instead of modulo. Resizes by doubling when load
factor reaches 1.0.

Ownership rule: `kv_set` takes ownership of the `kv_object` on success.
Caller must not free it. On failure, caller still owns it.

### RESP parser — the wire protocol

Parses Redis Serialization Protocol v2 incrementally. TCP delivers byte
streams, not message boundaries — the parser returns "need more" on
incomplete input and the dispatcher leaves the bytes in the read buffer
until the next read event.

```
*3\r\n          array of 3 elements
$3\r\nSET\r\n   bulk string "SET"
$3\r\nfoo\r\n   bulk string "foo"
$3\r\nbar\r\n   bulk string "bar"
```

All parsed strings carry explicit `len` — never use `strlen` on them,
they may contain NUL bytes.

### TCP server — the event loop

Single-threaded. Uses epoll on Linux, kqueue on macOS behind `#ifdef`.
One process, no threads, no locks on the store.

- Listening socket is non-blocking
- New connections set non-blocking, registered with epoll/kqueue
- `SIGPIPE` ignored at startup
- On read event: fill `rbuf`, drain RESP frames, dispatch each
- On write event: flush `wbuf`, deregister write interest when empty

### TTL — expiry

Two paths:

- **Passive**: every `kv_get` calls `ttl_check_expired` before returning.
  If expired, deletes the key and returns NULL.
- **Active**: `ttl_sweep` runs every ~100ms from the event loop tick.
  Samples random buckets within a time budget, cleans up without
  waiting for a client to access the key.

### LRU eviction

Triggered when memory exceeds `maxmemory` (set via `CONFIG SET maxmemory`).
Every access stamps `kv_object.lru` with the current monotonic clock.
Eviction samples 5 random keys, evicts the one with the smallest `lru`
value, repeats until under the limit. Supports `k`, `m`, `g` suffixes.

### AOF persistence

Every write command is appended to `memvault.aof` as raw RESP bytes
immediately after the handler succeeds. Read commands never write to AOF.

On startup, `aof_replay` feeds the file through the same RESP parser and
dispatches each command with a `replaying` flag set — handlers skip
`aof_append` while replaying to avoid re-logging. Survives `kill -9`.

---

## Build plan

| Step | What landed |
|------|-------------|
| 1 | Storage engine — `kv_object`, `kvstore`, CLI test driver |
| 2 | TCP server + RESP parser — `redis-cli` works against the server |
| 3 | TTL + expiry — `EXPIRE`, `TTL`, `PERSIST`, passive + active expiry |
| 4 | Lists and hashes — `LPUSH/LRANGE`, `HSET/HGETALL`, WRONGTYPE errors |
| 5 | LRU eviction — `CONFIG SET maxmemory`, sampled eviction policy |
| 6 | AOF persistence — append on write, replay on startup, `kill -9` survival |

---

## Conventions

- **Language:** C11. Flags: `-std=c11 -Wall -Wextra -Wpedantic -O2 -g`. Zero warnings.
- **Style:** 4-space indent, K&R braces, `snake_case`, public symbols prefixed by module.
- **Memory:** every `malloc` is checked. Ownership documented at every API boundary.
- **Types:** `size_t` for byte counts, `int64_t` for ms timestamps, `uint64_t` for counters.
- **Headers:** self-contained — each `.h` includes its own dependencies.

---

## Gotchas

- **RESP parsing is incremental.** Never assume a full command fits in one read.
- **SIGPIPE** — ignored at startup. A peer closing mid-write would otherwise kill the process.
- **EAGAIN is not an error.** On non-blocking sockets, `-1` with `errno == EAGAIN` means no data yet.
- **AOF replay must not re-append.** The `replaying` flag bypasses `aof_append` during startup.
- **Never `strlen` a RESP string.** Always use the explicit `len` field — values are binary-safe.
- **Never free what `kv_get` returns.** It is a borrowed pointer owned by the store.

---

## Supported commands

| Category | Commands |
|----------|----------|
| Server   | `PING` `ECHO` `DBSIZE` `FLUSHDB` `INFO` |
| Strings  | `SET` `GET` `DEL` `EXISTS` `TYPE` `KEYS` |
| Expiry   | `EXPIRE` `TTL` `PERSIST` |
| Lists    | `LPUSH` `RPUSH` `LPOP` `RPOP` `LLEN` `LRANGE` |
| Hashes   | `HSET` `HGET` `HDEL` `HLEN` `HGETALL` |
| Config   | `CONFIG GET` `CONFIG SET maxmemory` |
