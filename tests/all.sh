#!/usr/bin/env bash
set -uo pipefail
shopt -s expand_aliases
alias redis-cli=$'/c/Program\ Files/Redis/redis-cli.exe'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== kvault: running all test steps ==="

for step in step1 step2 step3 step4 step5 step6; do
    echo ""
    echo "---------- $step ----------"
    if bash "$SCRIPT_DIR/$step.sh"; then
        echo "$step PASSED"
    else
        echo "$step FAILED"
        exit 1
    fi
done

echo ""
echo "=== all steps passed ==="
