#!/bin/sh
# tests/lsp_smoke.sh - LSP contract check: initialize -> diagnostics -> shutdown.
set -u
cd "$(dirname "$0")/.."
RALT=./build/ralt

frame() {
    # frame <json-body> -> writes framed message to stdout
    local n
    n=$(printf '%s' "$1" | wc -c)
    printf 'Content-Length: %d\r\n\r\n%s' "$n" "$1"
}

body_init='{"jsonrpc":"2.0","id":9,"method":"initialize","params":{}}'
body_open='{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tests/cases/check_typemismatch.tp","languageId":"tripon","version":1,"text":"use tier core;\n\nfn main() -> i32 {\n    let x = 5 + \"hello\";\n    return 0;\n}\n"}}}'
body_shutdown='{"jsonrpc":"2.0","id":3,"method":"shutdown","params":{}}'
body_exit='{"jsonrpc":"2.0","method":"exit","params":{}}'

tmp=$(mktemp)
{
    frame "$body_init"
    frame "$body_open"
    frame "$body_shutdown"
    frame "$body_exit"
} | $RALT lsp > "$tmp" 2>/dev/null

# validate: initialize response echoes capabilties + serverInfo; diagnostics published
if ! grep -q '"serverInfo"' "$tmp"; then
    echo "FAIL: no initialize response"; exit 1
fi
if ! grep -q 'publishDiagnostics' "$tmp"; then
    echo "FAIL: no diagnostics published"; exit 1
fi
if ! grep -q 'E0202' "$tmp"; then
    echo "FAIL: E0202 not reported through LSP"; exit 1
fi
if ! grep -q '"id":9' "$tmp"; then
    echo "FAIL: initialize id not echoed"; exit 1
fi
echo "ok   lsp smoke (initialize, didOpen, publishDiagnostics, shutdown)"
