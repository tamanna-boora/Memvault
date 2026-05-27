#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step1: build with -Werror ==="

if make CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -O2 -g -Werror" 2>&1; then
    echo "PASS: build with -Werror"
    PASS=$((PASS + 1))
else
    echo "FAIL: build with -Werror"
    FAIL=$((FAIL + 1))
    summary
    exit 1
fi

echo ""
echo "=== step1: server startup ==="

start_server "./kvault.exe"
trap stop_server EXIT

assert_eq "PING returns PONG" "$(rc PING)" "PONG"

summary
