#!/usr/bin/env bash
# Shared test helpers — source this file; do not run directly.

shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

PORT=${PORT:-6380}
SERVER_PID=""
PASS=0
FAIL=0

start_server() {
    local bin="${1:-./kvault.exe}"
    "$bin" --port "$PORT" &
    SERVER_PID=$!
    local i=0
    until redis-cli --raw -p "$PORT" PING 2>/dev/null | grep -q PONG; do
        sleep 0.1
        i=$((i + 1))
        if [ "$i" -ge 60 ]; then
            echo "FAIL: server did not start within 6s"
            exit 1
        fi
    done
}

stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
        SERVER_PID=""
    fi
}

# run redis-cli with raw output
rc() {
    redis-cli --raw -p "$PORT" "$@"
}

assert_eq() {
    local desc="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc — got '$got', want '$want'"
        FAIL=$((FAIL + 1))
    fi
}

assert_contains() {
    local desc="$1" haystack="$2" needle="$3"
    if [[ "$haystack" == *"$needle"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc — '$haystack' does not contain '$needle'"
        FAIL=$((FAIL + 1))
    fi
}

assert_between() {
    local desc="$1" got="$2" lo="$3" hi="$4"
    if [ "$got" -ge "$lo" ] && [ "$got" -le "$hi" ]; then
        echo "PASS: $desc (got $got)"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc — got $got, want $lo..$hi"
        FAIL=$((FAIL + 1))
    fi
}

summary() {
    echo ""
    echo "Results: $PASS passed, $FAIL failed"
    [ "$FAIL" -eq 0 ]
}
