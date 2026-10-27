#!/bin/sh
# tests/determinism.sh - same input bytes must always produce the same .c.
# The whole agent contract rests on this.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
mkdir -p build/tests
src=tests/cases/hello.tp

$RALT emit --c "$src" -o build/tests/det1.c > /dev/null 2>&1 || { echo "FAIL: emit 1"; exit 1; }
$RALT emit --c "$src" -o build/tests/det2.c > /dev/null 2>&1 || { echo "FAIL: emit 2"; exit 1; }

if cmp -s build/tests/det1.c build/tests/det2.c; then
    echo "ok   determinism (byte-identical codegen)"
    exit 0
fi
echo "FAIL: codegen is not deterministic"
diff build/tests/det1.c build/tests/det2.c | head -20
exit 1
