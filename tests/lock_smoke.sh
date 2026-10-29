#!/bin/sh
# tests/lock_smoke.sh - content-addressed lockfile: `ralt lock` writes one
# "name hash" line per dep, deterministically, and the hash tracks content.
set -u
cd "$(dirname "$0")/.."
RALT=$(pwd)/build/ralt
TD=$(mktemp -d)
trap 'rm -rf "$TD"' EXIT

fail() { echo "FAIL lock: $1" >&2; exit 1; }

mkdir -p "$TD/depa" "$TD/depb"
printf 'fn a() -> i32 { return 1; }\n' > "$TD/depa/lib.tp"
printf 'fn b() -> i32 { return 2; }\n' > "$TD/depb/lib.tp"
printf 'name: p\nversion: 0.1.0\ndeps:\n  depa: %s/depa\n  depb: %s/depb\n' "$TD" "$TD" > "$TD/ralt.yaml"

(cd "$TD" && "$RALT" lock > /dev/null) || fail "lock failed"
[ -f "$TD/ralt.lock" ] || fail "no ralt.lock written"

# two lines, sorted by name, each "name 16 hex chars"
[ "$(wc -l < "$TD/ralt.lock")" -eq 2 ] || fail "expected 2 dep lines"
first=$(sed -n 1p "$TD/ralt.lock" | cut -d' ' -f1)
second=$(sed -n 2p "$TD/ralt.lock" | cut -d' ' -f1)
[ "$first" = depa ] && [ "$second" = depb ] || fail "lock not sorted: $first $second"
sed -n 1p "$TD/ralt.lock" | grep -Eq '^depa [0-9a-f]{16}$' || fail "bad line format"

cp "$TD/ralt.lock" "$TD/first.lock"
(cd "$TD" && "$RALT" lock > /dev/null) || fail "second lock failed"
cmp -s "$TD/first.lock" "$TD/ralt.lock" || fail "lock not deterministic"

printf 'fn a() -> i32 { return 99; }\n' > "$TD/depa/lib.tp"
(cd "$TD" && "$RALT" lock > /dev/null) || fail "third lock failed"
if cmp -s "$TD/first.lock" "$TD/ralt.lock"; then
    fail "hash did not change after editing a dep"
fi

echo "ok   lock (deterministic content hashes)"
