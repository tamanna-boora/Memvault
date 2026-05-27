#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step6: AOF kill-9 restart survival ==="

# Start fresh: remove any AOF from a previous run
rm -f appendonly.aof

start_server "./kvault.exe"

# Write a few keys
rc SET survive1 "hello"  >/dev/null
rc SET survive2 "world"  >/dev/null
rc RPUSH mylist a        >/dev/null
rc RPUSH mylist b        >/dev/null
rc RPUSH mylist c        >/dev/null
rc HSET myhash f1 v1     >/dev/null

# Give the event loop time to flush and fsync the AOF (fsync interval = 1s)
sleep 2

# Hard kill — simulate a crash
kill -9 "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""

echo "server killed; restarting to test AOF replay..."

# Restart — AOF replay runs automatically on startup
start_server "./kvault.exe"
trap stop_server EXIT

# Keys must have survived the crash via AOF replay
assert_eq "survive1"        "$(rc GET survive1)"    "hello"
assert_eq "survive2"        "$(rc GET survive2)"    "world"
assert_eq "mylist length"   "$(rc LLEN mylist)"     "3"
assert_eq "mylist lpop"     "$(rc LPOP mylist)"     "a"
assert_eq "myhash f1"       "$(rc HGET myhash f1)"  "v1"

summary
