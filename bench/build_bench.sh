#!/bin/sh
# bench/build_bench.sh - the "faster builder" contract, measured not vibed.
# Records wall-ms for building hello.world with the default zero-opt pipeline.
# Human-readable only; CI diffs land when the repo gets one.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt
mkdir -p build/tests bench

timed() {
    t0=$(date +%s%N 2>/dev/null || stat -c %N /dev/null)
    start=$(date +%s%N)
    "$@" > /dev/null 2>&1
    rc=$?
    end=$(date +%s%N)
    ms=$(( (end - start) / 1000000 ))
    echo "$ms"
    return $rc
}

full=$(timed $RALT build tests/cases/hello.tp -o build/tests/bench_hello)
half=$(timed $RALT check tests/cases/hello.tp)
emit=$(timed $RALT emit --c tests/cases/hello.tp -o build/tests/bench.c)

size=$(stat -c %s build/tests/bench_hello 2>/dev/null || wc -c < build/tests/bench_hello)

printf 'build full pipeline : %s ms\ncheck only          : %s ms\nemit c only         : %s ms\nbinary size         : %s bytes\n' \
    "$full" "$half" "$emit" "$size"
