#!/bin/sh
# selfhost oracle tests:
#  P3.0: the Tripon lexer port replays the bootstrap C lexer token stream
#  P3.1: the Tripon parser port replays the bootstrap AST tree dump
# Input is the file's exact bytes via $TP_SRC (sentinel keeps trailing
# newlines, which $(cat) would otherwise strip).
set -e
cd "$(dirname "$0")/.."
RALT="${RALT:-./build/ralt}"
OUT="${OUT:-/tmp}"
"$RALT" build selfhost/lexer.tp -o "$OUT/tp_lexbin"
"$RALT" build selfhost/parser.tp -o "$OUT/tp_parbin"
n=0
fail=0
for f in examples/*.tp tests/cases/*.tp selfhost/lexer.tp; do
    n=$((n + 1))
    TP_SRC="$(cat "$f"; echo x)"
    TP_SRC="${TP_SRC%x}"
    export TP_SRC
    exp="$("$RALT" tokens "$f" || true)"
    got="$("$OUT/tp_lexbin" 2>&1)"
    if [ "$exp" != "$got" ]; then
        echo "FAIL selfhost lexer $f"
        fail=$((fail + 1))
    fi
    unset TP_SRC
done
if [ "$fail" -ne 0 ]; then
    echo "selfhost lexer: $fail/$n mismatched"
    [ "$fail" -eq 0 ]
fi
echo "ok   selfhost lexer ($n files)"

n=0
fail=0
for f in examples/*.tp tests/cases/*.tp selfhost/lexer.tp selfhost/parser.tp; do
    n=$((n + 1))
    TP_SRC="$(cat "$f"; echo x)"
    TP_SRC="${TP_SRC%x}"
    export TP_SRC
    exp="$("$RALT" tree "$f" || true)"
    got="$("$OUT/tp_parbin" 2>&1)"
    if [ "$exp" != "$got" ]; then
        echo "FAIL selfhost parser $f"
        fail=$((fail + 1))
    fi
    unset TP_SRC
done
if [ "$fail" -ne 0 ]; then
    echo "selfhost parser: $fail/$n mismatched"
    [ "$fail" -eq 0 ]
fi
echo "ok   selfhost parser ($n files)"
