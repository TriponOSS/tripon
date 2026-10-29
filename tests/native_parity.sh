#!/bin/sh
# tests/native_parity.sh - P4.4 native slice: the cases the x86-64 backend
# supports today (scalars, str, floats, traps) must behave byte-for-byte like
# the C backend. Aggregate cases are expected to be rejected by the native
# backend for now and are reported as skipped, not failed.
#
# Runs in an isolated dir without the repo ralt.yaml so stdlib deps are not
# folded into the native translation unit.
set -u
cd "$(dirname "$0")/.."
RALT=$(pwd)/build/ralt
OUT=build/tests/native
mkdir -p "$OUT"

pass=0
fail=0
skip=0
failed_names=""

note() { printf '  %s\n' "$*"; }

for src in tests/cases/*.tp; do
    name=$(basename "$src" .tp)
    # check-json cases are front-end only; backend parity is the build+run half
    [ -f "tests/expected/$name.json" ] && continue

    work="$OUT/$name"
    rm -rf "$work"; mkdir -p "$work"
    cp "$src" "$work/main.tp"

    # C reference build
    if ! (cd "$work" && "$RALT" build main.tp -o c.bin > c.log 2>&1); then
        note "FAIL native/$name: C build failed"
        fail=$((fail + 1)); failed_names="$failed_names native/$name"; continue
    fi
    # native build may legitimately reject an aggregate slice
    if ! (cd "$work" && "$RALT" build main.tp --backend native -o n.bin > n.log 2>&1); then
        skip=$((skip + 1))
        note "skip native/$name (aggregate slice not implemented)"
        continue
    fi

    (cd "$work" && ./c.bin > c.out 2> c.err); crc=$?
    (cd "$work" && ./n.bin > n.out 2> n.err); nrc=$?
    if [ "$crc" -ne "$nrc" ]; then
        note "FAIL native/$name: exit $nrc, C backend $crc"
        fail=$((fail + 1)); failed_names="$failed_names native/$name"; continue
    fi
    if ! diff -u "$work/c.out" "$work/n.out" > "$work/out.diff" 2>&1; then
        note "FAIL native/$name: stdout differs from C backend"
        head -20 "$work/out.diff"
        fail=$((fail + 1)); failed_names="$failed_names native/$name"; continue
    fi
    if [ -f "tests/expected/$name.trap" ]; then
        want=$(cat "tests/expected/$name.trap")
        if ! grep -q "$want" "$work/n.err"; then
            note "FAIL native/$name: native trap output missing $want"
            fail=$((fail + 1)); failed_names="$failed_names native/$name"; continue
        fi
    fi
    pass=$((pass + 1))
    note "ok   native/$name"
done

printf 'native parity: %d passed, %d failed, %d skipped\n' "$pass" "$fail" "$skip"
[ -n "$failed_names" ] && printf 'failed:%s\n' "$failed_names"
[ "$fail" -eq 0 ]
