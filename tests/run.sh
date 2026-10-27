#!/bin/sh
# tests/run.sh - Tripon P1 test suite. POSIX sh, zero deps, deterministic.
#
# Contract per case name N in tests/cases/:
#   tests/expected/N.out   -> run the binary, stdout must match exactly, exit 0
#   tests/expected/N.trap  -> binary must exit 101 and stderr must contain that file's code
#   tests/expected/N.json  -> `ralt check --json` output must match exactly (agent contract)
#   a case may combine .out and .json only if it also has warnings (unused/etc)
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
OUT=build/tests
mkdir -p "$OUT"

pass=0
fail=0
failed_names=""

note() { printf '  %s\n' "$*"; }

for src in tests/cases/*.tp; do
    name=$(basename "$src" .tp)
    : > "$OUT/$name.log"

    # --- check contract ---
    if [ -f "tests/expected/$name.json" ]; then
        $RALT check --json "$src" > "$OUT/$name.json" 2>>"$OUT/$name.log"
        rc=$?
        want_rc=0
        grep -q '"kind": "error"' "tests/expected/$name.json" && want_rc=1
        if [ "$rc" -ne "$want_rc" ]; then
            note "FAIL $name: check exit $rc, wanted $want_rc"
            cat "$OUT/$name.log"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        if diff -u "tests/expected/$name.json" "$OUT/$name.json" > "$OUT/$name.diff" 2>&1; then
            note "ok   $name (check json)"
            pass=$((pass + 1))
        else
            note "FAIL $name: check json mismatch"
            head -40 "$OUT/$name.diff"
            fail=$((fail + 1)); failed_names="$failed_names $name"
        fi
        continue
    fi

    # --- build + run contract ---
    if ! $RALT build "$src" -o "$OUT/$name" > "$OUT/$name.log" 2>&1; then
        note "FAIL $name: ralt build failed"
        cat "$OUT/$name.log"
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi

    if [ -f "tests/expected/$name.trap" ]; then
        want_code=$(cat "tests/expected/$name.trap")
        "./$OUT/$name" > "$OUT/$name.stdout" 2>>"$OUT/$name.log"
        rc=$?
        if [ "$rc" -ne 101 ]; then
            note "FAIL $name: expected trap exit 101, got $rc"
            cat "$OUT/$name.log"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        if ! grep -q "$want_code" "$OUT/$name.log"; then
            note "FAIL $name: expected $want_code in trap output"
            cat "$OUT/$name.log"
            fail=$((fail + 1)); failed_names="$failed_names $name"; continue
        fi
        note "ok   $name (trap $want_code)"
        pass=$((pass + 1))
        continue
    fi

    "./$OUT/$name" > "$OUT/$name.stdout" 2>>"$OUT/$name.log"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        note "FAIL $name: runtime exit $rc"
        cat "$OUT/$name.log"
        fail=$((fail + 1)); failed_names="$failed_names $name"; continue
    fi
    if diff -u "tests/expected/$name.out" "$OUT/$name.stdout" > "$OUT/$name.diff" 2>&1; then
        note "ok   $name"
        pass=$((pass + 1))
    else
        note "FAIL $name: stdout mismatch"
        head -40 "$OUT/$name.diff"
        fail=$((fail + 1)); failed_names="$failed_names $name"
    fi
done

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ -n "$failed_names" ] && printf 'failed:%s\n' "$failed_names"
[ "$fail" -eq 0 ]
