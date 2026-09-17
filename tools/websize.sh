#!/bin/sh
# Web build sizes per example: wasm and JS glue, raw and compressed (gzip -9, and
# brotli when the `brotli` tool is installed), sorted by total download size.
#
#   tools/websize.sh examples/build/webgl2      (also: make websize [BACKEND=...])
#
# Writes the table to <site>/sizes.txt as well.
set -eu
SITE="${1:?usage: tools/websize.sh <web build directory>}"
[ -d "$SITE" ] || { echo "websize: no web build at $SITE" >&2; exit 1; }
HAVE_BROTLI=$(command -v brotli >/dev/null 2>&1 && echo 1 || echo 0)

gz() { gzip -9c "$1" | wc -c; }
br() { brotli -q 11 -c "$1" | wc -c; }
kb() { awk -v b="$1" 'BEGIN { printf "%.1f", b / 1024 }'; }

rows=$(
    for js in "$SITE"/*.js; do
        name=$(basename "$js" .js)
        wasm="$SITE/$name.wasm"
        [ -f "$wasm" ] || continue
        w=$(wc -c < "$wasm"); j=$(wc -c < "$js")
        wg=$(gz "$wasm"); jg=$(gz "$js")
        if [ "$HAVE_BROTLI" = 1 ]; then wb=$(br "$wasm"); jb=$(br "$js"); else wb=0; jb=0; fi
        echo "$name $w $j $wg $jg $wb $jb $((wg + jg)) $((wb + jb))"
    done | sort -k8 -n
)
[ -n "$rows" ] || { echo "websize: no examples in $SITE" >&2; exit 1; }

{
    echo "web sizes: $SITE (KB)"
    if [ "$HAVE_BROTLI" = 1 ]; then
        printf "%-16s %9s %9s %9s %9s %9s %9s %10s %10s\n" example wasm js wasm.gz js.gz wasm.br js.br "gz total" "br total"
    else
        printf "%-16s %9s %9s %9s %9s %10s\n" example wasm js wasm.gz js.gz "gz total"
    fi
    echo "$rows" | while read -r name w j wg jg wb jb total brtotal; do
        if [ "$HAVE_BROTLI" = 1 ]; then
            printf "%-16s %9s %9s %9s %9s %9s %9s %10s %10s\n" "$name" "$(kb "$w")" "$(kb "$j")" "$(kb "$wg")" \
                "$(kb "$jg")" "$(kb "$wb")" "$(kb "$jb")" "$(kb "$total")" "$(kb "$brtotal")"
        else
            printf "%-16s %9s %9s %9s %9s %10s\n" "$name" "$(kb "$w")" "$(kb "$j")" "$(kb "$wg")" "$(kb "$jg")" \
                "$(kb "$total")"
        fi
    done
    [ "$HAVE_BROTLI" = 1 ] || echo "(install brotli for brotli sizes)"
} | tee "$SITE/sizes.txt"
