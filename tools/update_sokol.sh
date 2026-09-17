#!/bin/sh
# Update the vendored sokol headers in deps/sokol from libsk's sokol fork.
#
#   tools/update_sokol.sh [ref]     ref: a branch, tag or commit (default: master)
#
# The fork (SOKOL_REPO, default github.com/robknopf/sokol) is floooh/sokol plus
# fixes libsk needs; sync it with upstream there, then run this. Only the headers
# already in deps/sokol are copied. deps/sokol/VERSION records the fork commit and
# the upstream commit it's based on. Review the diff, rebuild (make verify, make
# webcheck), then commit.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${SOKOL_REPO:-https://github.com/robknopf/sokol.git}"
UPSTREAM="https://github.com/floooh/sokol.git"
REF="${1:-master}"
DEST="$ROOT/deps/sokol"
SRC="$ROOT/build/sokol-src"

if [ ! -d "$SRC/.git" ]; then
    git clone --quiet "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet origin
git -C "$SRC" fetch --quiet "$UPSTREAM" master
git -C "$SRC" checkout --quiet --detach "$(git -C "$SRC" rev-parse --verify "origin/$REF" 2>/dev/null || echo "$REF")"
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
DATE="$(git -C "$SRC" log -1 --format=%cs HEAD)"
BASE="$(git -C "$SRC" merge-base HEAD FETCH_HEAD)"

for file in "$DEST"/*.h "$DEST"/util/*.h; do
    name="${file#"$DEST"/}"
    if [ ! -f "$SRC/$name" ]; then
        echo "update_sokol: $name is no longer in sokol" >&2
        exit 1
    fi
    cp "$SRC/$name" "$file"
done

SHDC_LINE="$(grep '^sokol-tools-bin ' "$DEST/VERSION" || true)"
{
    echo "# Vendored sokol headers, from libsk's fork of floooh/sokol (fixes libsk needs on"
    echo "# top of upstream). Only the files in this directory are vendored; update them all"
    echo "# together with tools/update_sokol.sh."
    echo "sokol-fork $REPO $COMMIT $DATE"
    echo "sokol-upstream $UPSTREAM $BASE"
    echo "# Shaders in src/shaders/*.glsl.h are generated with sokol-shdc from"
    echo "# https://github.com/floooh/sokol-tools-bin (tools/sokol-shdc, not committed):"
    [ -n "$SHDC_LINE" ] && echo "$SHDC_LINE"
} > "$DEST/VERSION"

echo "update_sokol: deps/sokol now at fork $COMMIT ($DATE), upstream base $BASE"
git -C "$ROOT" diff --stat -- deps/sokol
