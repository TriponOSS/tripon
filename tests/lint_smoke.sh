#!/bin/sh
# tests/lint_smoke.sh - tier linter: features above the declared ceiling are
# E0301, features at or below it are clean. Parse-only, so no main required.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
OUT=build/tests
mkdir -p "$OUT"

pass=0
fail=0
failed_names=""
note() { printf '  %s\n' "$*"; }

expect_clean() {
    name=$1
    if $RALT lint "tests/lint/$name.tp" > "$OUT/lint_$name.log" 2>&1; then
        note "ok   lint/$name (clean)"
        pass=$((pass + 1))
    else
        note "FAIL lint/$name: expected clean"
        cat "$OUT/lint_$name.log"
        fail=$((fail + 1)); failed_names="$failed_names lint/$name"
    fi
}

expect_violation() {
    name=$1
    $RALT lint --json "tests/lint/$name.tp" > "$OUT/lint_$name.json" 2>"$OUT/lint_$name.log"
    rc=$?
    if [ "$rc" -eq 1 ] && grep -q '"code": "E0301"' "$OUT/lint_$name.json"; then
        note "ok   lint/$name (E0301)"
        pass=$((pass + 1))
    else
        note "FAIL lint/$name: expected E0301, rc=$rc"
        cat "$OUT/lint_$name.json" "$OUT/lint_$name.log"
        fail=$((fail + 1)); failed_names="$failed_names lint/$name"
    fi
}

expect_clean kernel_ok
expect_clean core_opt
expect_violation kernel_opt
expect_violation kernel_str
expect_violation kernel_match

printf 'lint: %d passed, %d failed\n' "$pass" "$fail"
[ -n "$failed_names" ] && printf 'failed:%s\n' "$failed_names"
[ "$fail" -eq 0 ]
