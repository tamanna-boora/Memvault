#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"
cd "$ROOT"

echo "=== step2: PING SET GET DEL EXISTS ECHO ==="

start_server "./kvault.exe"
trap stop_server EXIT

assert_eq "PING"              "$(rc PING)"           "PONG"
assert_eq "ECHO hello"        "$(rc ECHO hello)"     "hello"

assert_eq "SET foo bar"       "$(rc SET foo bar)"    "OK"
assert_eq "GET foo"           "$(rc GET foo)"        "bar"
assert_eq "EXISTS foo"        "$(rc EXISTS foo)"     "1"

assert_eq "DEL foo"           "$(rc DEL foo)"        "1"
assert_eq "GET foo (missing)" "$(rc GET foo)"        ""
assert_eq "EXISTS foo (gone)" "$(rc EXISTS foo)"     "0"
assert_eq "DEL missing key"   "$(rc DEL nosuchkey)"  "0"

# SET overwrites existing key
rc SET counter 1 >/dev/null
rc SET counter 2 >/dev/null
assert_eq "SET overwrites"    "$(rc GET counter)"    "2"

# Unknown command must return an error
ERR=$(rc NOSUCHCMD 2>&1 || true)
assert_contains "unknown command → ERR" "$ERR" "ERR"

summary
