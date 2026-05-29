# Memvault

A Redis-inspired, single-threaded, in-memory key-value store built from scratch in C11.
Speaks the RESP2 wire protocol — so `redis-cli` works as the client out of the box.

---

## Features

- **Strings, Lists, Hashes** — core data types with full command support
- **TTL / expiry** — passive expiry on access + active background sweep every 100ms
- **LRU eviction** — sampled eviction when memory exceeds `maxmemory`
- **AOF persistence** — every write appended to disk, replayed on startup, survives `kill -9`
- **RESP2 protocol** — compatible with `redis-cli` and any Redis client library
- **Single-threaded event loop** — epoll on Linux, kqueue on macOS

---

## Build

Requires a C11 compiler (`gcc` or `clang`) and POSIX. No third-party dependencies.

```sh
git clone https://github.com/tamanna-boora/Memvault.git
cd Memvault
make
```

### Windows (Git Bash / MINGW64)

```sh
make
./memvault.exe --port 6380
```

---

## Run

```sh
./memvault --port 6380
```

Default port is `6380` to avoid clashing with a local Redis on `6379`.

---

## Connect

```sh
redis-cli -p 6380 PING        # PONG
redis-cli -p 6380 SET name alice  # OK
redis-cli -p 6380 GET name    # "alice"
```

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

---

## Quick test

```sh
# Strings
redis-cli -p 6380 SET city london
redis-cli -p 6380 GET city          # "london"

# TTL
redis-cli -p 6380 EXPIRE city 5
redis-cli -p 6380 TTL city          # 5
# wait 6 seconds
redis-cli -p 6380 GET city          # (nil)

# Lists
redis-cli -p 6380 RPUSH colors red green blue
redis-cli -p 6380 LRANGE colors 0 -1   # red green blue

# Hashes
redis-cli -p 6380 HSET user:1 name alice age 25
redis-cli -p 6380 HGETALL user:1    # name alice age 25
```

---

## Persistence

Memvault appends every write command to `memvault.aof` as raw RESP bytes.
On startup it replays the file through the same command dispatcher — the
database restores itself from the command history. Survives `kill -9`.

```sh
./memvault --port 6380 --aof-path ./memvault.aof
```

---

## Eviction

```sh
redis-cli -p 6380 CONFIG SET maxmemory 64mb
```

Supports `k`, `m`, `g` suffixes. Policy is `allkeys-lru` — the coldest
key (least recently used) is evicted first when memory exceeds the limit.

---

## Project layout

```
src/
├── main.c          entry point, arg parsing, AOF replay
├── server.c/h      event loop, accept, client tracking
├── client.c/h      per-connection read/write buffers
├── resp.c/h        incremental RESP2 parser + serializer
├── commands.c/h    command table and handlers
├── kvstore.c/h     main hash table (FNV-1a, power-of-2 buckets)
├── object.c/h      value wrapper: type, payload, LRU clock, TTL
├── kvlist.c/h      doubly linked list (LIST type)
├── kvhash.c/h      nested hash table (HASH type)
├── ttl.c/h         expiry tracking and active sweeper
├── eviction.c/h    LRU eviction policy
├── aof.c/h         append-only file: write and replay
└── util.c/h        logging and helpers
```

For internals, design decisions, and gotchas see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Build flags

```
-std=c11 -Wall -Wextra -Wpedantic -O2 -g
```

Zero warnings enforced. No third-party libraries — just libc and POSIX.
