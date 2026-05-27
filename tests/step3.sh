#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step3: EXPIRE TTL PERSIST ==="

start_server "./kvault.exe"
trap stop_server EXIT

rc SET mykey myval >/dev/null

assert_eq "TTL (no expiry)"     "$(rc TTL mykey)"     "-1"
assert_eq "PERSIST (no expiry)" "$(rc PERSIST mykey)" "0"

assert_eq "EXPIRE mykey 10"     "$(rc EXPIRE mykey 10)" "1"
assert_between "TTL in range"   "$(rc TTL mykey)" 8 10

assert_eq "PERSIST clears expiry" "$(rc PERSIST mykey)" "1"
assert_eq "TTL after PERSIST"     "$(rc TTL mykey)"     "-1"

# Passive expiry: set a 1s TTL and wait 3 seconds
rc SET expkey willdie >/dev/null
rc EXPIRE expkey 1 >/dev/null

sleep 3

assert_eq "GET after expiry"    "$(rc GET expkey)"    ""
assert_eq "TTL after expiry"    "$(rc TTL expkey)"    "-2"
assert_eq "EXISTS after expiry" "$(rc EXISTS expkey)" "0"

summary
