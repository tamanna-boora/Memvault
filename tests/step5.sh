#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step5: maxmemory eviction ==="

start_server "./kvault.exe"
trap stop_server EXIT

# Set a tight memory limit
assert_eq "CONFIG SET maxmemory 5kb" "$(rc CONFIG SET maxmemory 5kb)" "OK"

# Write 150 keys with ~80-byte values — well over the 5kb limit
PADDING="xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
for i in $(seq 1 150); do
    rc SET "k$i" "$PADDING" >/dev/null 2>&1 || true
done

# Count surviving keys — eviction must have removed some
SURVIVED=0
for i in $(seq 1 150); do
    [ "$(rc EXISTS "k$i")" = "1" ] && SURVIVED=$((SURVIVED + 1)) || true
done

if [ "$SURVIVED" -lt 150 ]; then
    echo "PASS: eviction occurred ($SURVIVED/150 keys remain)"
    PASS=$((PASS + 1))
else
    echo "FAIL: no eviction — all 150 keys still present"
    FAIL=$((FAIL + 1))
fi

# CONFIG GET should report the current limit
CFG=$(rc CONFIG GET maxmemory)
assert_contains "CONFIG GET maxmemory" "$CFG" "maxmemory"

summary
