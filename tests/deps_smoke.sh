#!/bin/sh
# deps smoke test: exercises manifest parse + init template + collect logic
# by compiling ralt/src/deps.c against a throwaway test main (driver not wired yet).
set -e
cd "$(dirname "$0")/.."
REPO="$PWD"
TD="/tmp/ralt_deps_smoke.$$"
trap 'rm -rf "$TD"' EXIT
mkdir -p "$TD"
TM="$TD/testmain.c"
BIN="$TD/ralt_depstest"

cat >"$TM" <<'EOF'
#include "common.h"
#include <string.h>
int deps_manifest_exists(void);
int deps_init_template(void);
int deps_collect(char ***paths, int *count);
void deps_free(char ***paths, int count);
int main(int argc, char **argv) {
    if (argc < 2) return 9;
    if (strcmp(argv[1], "exists") == 0) {         /* print how much is a lie; 0 -> no manifest */
        int r = deps_manifest_exists();
        printf("exists=%d\n", r);
        return 0;
    }
    if (strcmp(argv[1], "init") == 0) {
        int r = deps_init_template();
        printf("init=%d\n", r);
        return 0;
    }
    if (strcmp(argv[1], "collect") == 0) {
        char **paths = NULL; int count = 0;
        if (deps_collect(&paths, &count) != 0) return 2;
        printf("count=%d\n", count);
        for (int i = 0; i < count; i++) printf("%s\n", paths[i]);
        deps_free(&paths, count);
        return 0;
    }
    return 9;
}
EOF

fail() { echo "FAIL: $1" >&2; exit 1; }

cc -std=c99 -O0 -I ralt/src -o "$BIN" "$TM" ralt/src/deps.c ralt/src/util.c ralt/src/diag.c \
    || fail "test driver compile failed"

# --- fixture ---
mkdir -p "$TD/depa" "$TD/depb/tests" "$TD/proj"
printf 'fn plus_one(x: i32) -> i32 { return x + 1; }\n' > "$TD/depa/lib.tp"
printf 'fn plus_two(x: i32) -> i32 { return x + 2; }\n' > "$TD/depb/lib.tp"
printf 'fn repotest() {}\n' > "$TD/depb/tests/skipme.tp"
printf 'deps:\n  depa: %s\n  depb: %s\n' "$TD/depa" "$TD/depb" > "$TD/proj/ralt.yaml"
printf 'fn main() -> i32 { return 0; }\n' > "$TD/proj/main.tp"

# --- template init in a fresh dir ---
mkdir -p "$TD/fresh"
out="$(cd "$TD/fresh" && "$BIN" init)" || fail "init crashed: $out"
[ "$(echo "$out" | tail -1)" = "init=0" ] || fail "init fresh should be 0, got $out"
[ -f "$TD/fresh/ralt.yaml" ] || fail "template not written"
out="$(cd "$TD/fresh" && "$BIN" init)"
[ "$(echo "$out" | tail -1)" = "init=2" ] || fail "init overwrite should be 2, got $out"

# --- manifest exists probe ---
out="$(cd "$TD/fresh" && "$BIN" exists)"
[ "$(echo "$out" | tail -1)" = "exists=1" ] || fail "exists should be 1 in fresh, got $out"
out="$(cd "$TD/emptyprobe" 2>/dev/null && true || { mkdir -p "$TD/emptyprobe"; cd "$TD/emptyprobe"; } && "$BIN" exists)"
[ "$(echo "$out" | tail -1)" = "exists=0" ] || fail "exists should be 0 without manifest, got $out"

# --- collect in project dir ---
out="$(cd "$TD/proj" && "$BIN" collect)" || fail "collect failed: $out"
echo "$out" | grep -qx 'count=2' || fail "expected count=2, got: $out"
p1="$(echo "$out" | sed -n 2p)"
p2="$(echo "$out" | sed -n 3p)"
case "$p1" in *depa/*) ;; *) fail "first path not from depa: $p1";; esac
case "$p2" in *depb/*) ;; *) fail "second path not from depb: $p2";; esac
case "$out" in *tests*|*skipme*) fail "tests/ file not skipped: $out";; esac
[ "$p1" \< "$p2" ] || fail "paths not sorted: $p1 after $p2"

# --- malformed manifest ---
printf 'name: x\nbogus\n' > "$TD/proj/ralt.yaml"
out="$(cd "$TD/proj" && "$BIN" collect 2>&1)" && fail "malformed accepted: $out"
echo "$out" | grep -q 'ralt.yaml:2' || fail "no line number in error: $out"

echo "ok deps smoke"
