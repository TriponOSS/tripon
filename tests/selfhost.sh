#!/bin/sh
# selfhost oracle test (P3.0): the Tripon lexer port must reproduce the
# bootstrap C lexer's token stream byte for byte for every .tp file.
# Input is the file's exact bytes via $TP_SRC (sentinel keeps trailing
# newlines, which $(cat) would otherwise strip).
set -e
cd "$(dirname "$0")/.."
RALT="${RALT:-./build/ralt}"
n=0
fail=0
for f in examples/*.tp tests/cases/*.tp selfhost/lexer.tp; do
    n=$((n + 1))
    TP_SRC="$(cat "$f"; echo x)"
    TP_SRC="${TP_SRC%x}"
    export TP_SRC
    exp="$("$RALT" tokens "$f")"
    got="$("$RALT" run selfhost/lexer.tp 2>&1)"
    if [ "$exp" != "$got" ]; then
        echo "FAIL selfhost $f"
        fail=$((fail + 1))
    fi
    unset TP_SRC
done
if [ "$fail" -ne 0 ]; then
    echo "selfhost lexer: $fail/$n mismatched"
    [ "$fail" -eq 0 ]
fi
echo "ok   selfhost lexer ($n files)"
