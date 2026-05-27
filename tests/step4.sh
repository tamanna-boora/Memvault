#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step4: LIST commands ==="

start_server "./kvault.exe"
trap stop_server EXIT

assert_eq "RPUSH l a"    "$(rc RPUSH l a)"  "1"
assert_eq "RPUSH l b"    "$(rc RPUSH l b)"  "2"
assert_eq "LPUSH l z"    "$(rc LPUSH l z)"  "3"
assert_eq "LLEN l"       "$(rc LLEN l)"     "3"
assert_eq "LPOP l"       "$(rc LPOP l)"     "z"
assert_eq "RPOP l"       "$(rc RPOP l)"     "b"
assert_eq "LLEN l after" "$(rc LLEN l)"     "1"
assert_eq "LPOP last"    "$(rc LPOP l)"     "a"
assert_eq "LPOP empty"   "$(rc LPOP l)"     ""

echo ""
echo "=== step4: HASH commands ==="

assert_eq "HSET h f1 v1"       "$(rc HSET h f1 v1)"    "1"
assert_eq "HSET h f2 v2"       "$(rc HSET h f2 v2)"    "1"
assert_eq "HGET h f1"          "$(rc HGET h f1)"       "v1"
assert_eq "HGET h f2"          "$(rc HGET h f2)"       "v2"
assert_eq "HLEN h"             "$(rc HLEN h)"          "2"
assert_eq "HEXISTS h f1"       "$(rc HEXISTS h f1)"    "1"
assert_eq "HEXISTS h missing"  "$(rc HEXISTS h nofield)" "0"
assert_eq "HDEL h f2"          "$(rc HDEL h f2)"       "1"
assert_eq "HLEN h after del"   "$(rc HLEN h)"          "1"
assert_eq "HGET h f2 (gone)"   "$(rc HGET h f2)"       ""
# Update existing field
assert_eq "HSET h f1 new"      "$(rc HSET h f1 updated)" "0"
assert_eq "HGET h f1 updated"  "$(rc HGET h f1)"       "updated"

echo ""
echo "=== step4: WRONGTYPE errors ==="

rc SET strkey "hello" >/dev/null
ERR=$(rc LPUSH strkey x 2>&1 || true)
assert_contains "LPUSH on string → WRONGTYPE" "$ERR" "WRONGTYPE"

ERR=$(rc HSET strkey f v 2>&1 || true)
assert_contains "HSET on string → WRONGTYPE"  "$ERR" "WRONGTYPE"

ERR=$(rc LPUSH h val 2>&1 || true)
assert_contains "LPUSH on hash → WRONGTYPE"   "$ERR" "WRONGTYPE"

ERR=$(rc GET h 2>&1 || true)
assert_contains "GET on hash → WRONGTYPE"     "$ERR" "WRONGTYPE"

summary
