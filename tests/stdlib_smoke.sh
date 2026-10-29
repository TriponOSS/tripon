#!/bin/sh
# tests/stdlib_smoke.sh - stdlib smoke via ralt.yaml local-dep folding.
# Each tests/cases_std/*.tp runs with the repo stdlib/ folded in as a dep;
# stdout must match tests/expected_std/<name>.out byte-for-byte.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
OUT=build/tests
mkdir -p "$OUT" "$OUT/stdlibwork"

pass=0
fail=0
failed_names=""

note() { printf '  %s\n' "$*"; }

for src in tests/cases_std/*.tp; do
    name=$(basename "$src" .tp)
    work="$OUT/stdlibwork/$name"
    mkdir -p "$work"
    cp "$src" "$work/main.tp"
    printf 'name: %s\nversion: 0.1.0\ndeps:\n  std: %s/stdlib\n' "$name" "$PWD" > "$work/ralt.yaml"
    if ! (cd "$work" && "$OLDPWD/$RALT" run main.tp > "$OLDPWD/$OUT/$name.stdout" 2>"$OLDPWD/$OUT/$name.log"); then
        OLDPWD="$PWD"
        note "FAIL std/$name: ralt run failed"
        cat "$OUT/$name.log"
        fail=$((fail + 1)); failed_names="$failed_names std/$name"; continue
    fi
    if diff -u "tests/expected_std/$name.out" "$OUT/$name.stdout" > "$OUT/$name.diff" 2>&1; then
        note "ok   std/$name"
        pass=$((pass + 1))
    else
        note "FAIL std/$name: stdout mismatch"
        head -40 "$OUT/$name.diff"
        fail=$((fail + 1)); failed_names="$failed_names std/$name"
    fi
done

printf 'stdlib: %d passed, %d failed\n' "$pass" "$fail"
[ -n "$failed_names" ] && printf 'failed:%s\n' "$failed_names"
[ "$fail" -eq 0 ]
