#!/bin/sh
# tests/module_smoke.sh - phase-1 file modules:
#   use "path.tp" as alias;  +  alias::pub_fn(...)
# Checks the happy path, `pub` enforcement (E0702), missing module (E0703),
# duplicate alias (E0704), and that `ralt fmt` preserves the use line.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
OUT=build/tests
mkdir -p "$OUT"

pass=0
fail=0
failed_names=""
note() { printf '  %s\n' "$*"; }

# happy path: build + run
if $RALT run tests/modules/main.tp > "$OUT/modules_main.out" 2>"$OUT/modules_main.log" &&
   diff -u tests/expected/modules_main.out "$OUT/modules_main.out" > "$OUT/modules_main.diff" 2>&1; then
    note "ok   module/import"
    pass=$((pass + 1))
else
    note "FAIL module/import"
    cat "$OUT/modules_main.log" "$OUT/modules_main.diff" 2>/dev/null
    fail=$((fail + 1)); failed_names="$failed_names module/import"
fi

# private fn is not exported
$RALT check --json tests/modules/private.tp > "$OUT/modules_private.json" 2>/dev/null
rc=$?
if [ "$rc" -eq 1 ] && grep -q '"code": "E0702"' "$OUT/modules_private.json"; then
    note "ok   module/pub-only"
    pass=$((pass + 1))
else
    note "FAIL module/pub-only (rc=$rc)"
    cat "$OUT/modules_private.json"
    fail=$((fail + 1)); failed_names="$failed_names module/pub-only"
fi

# missing module file
$RALT check --json tests/modules/missing.tp > "$OUT/modules_missing.json" 2>/dev/null
rc=$?
if [ "$rc" -eq 1 ] && grep -q '"code": "E0703"' "$OUT/modules_missing.json"; then
    note "ok   module/not-found"
    pass=$((pass + 1))
else
    note "FAIL module/not-found (rc=$rc)"
    cat "$OUT/modules_missing.json"
    fail=$((fail + 1)); failed_names="$failed_names module/not-found"
fi

# duplicate alias
$RALT check --json tests/modules/dup.tp > "$OUT/modules_dup.json" 2>/dev/null
rc=$?
if [ "$rc" -eq 1 ] && grep -q '"code": "E0704"' "$OUT/modules_dup.json"; then
    note "ok   module/dup-alias"
    pass=$((pass + 1))
else
    note "FAIL module/dup-alias (rc=$rc)"
    cat "$OUT/modules_dup.json"
    fail=$((fail + 1)); failed_names="$failed_names module/dup-alias"
fi

# fmt keeps the import line and does not inline the module
$RALT fmt tests/modules/main.tp > "$OUT/modules_fmt.tp" 2>/dev/null
if grep -q 'use "lib/math.tp" as math;' "$OUT/modules_fmt.tp" &&
   ! grep -q 'fn helper' "$OUT/modules_fmt.tp"; then
    note "ok   module/fmt"
    pass=$((pass + 1))
else
    note "FAIL module/fmt"
    cat "$OUT/modules_fmt.tp"
    fail=$((fail + 1)); failed_names="$failed_names module/fmt"
fi

printf 'modules: %d passed, %d failed\n' "$pass" "$fail"
[ -n "$failed_names" ] && printf 'failed:%s\n' "$failed_names"
[ "$fail" -eq 0 ]
