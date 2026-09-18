#!/bin/sh
# Update the vendored Clay in deps/clay from libsk's Clay fork.
#
#   tools/update_clay.sh [ref]     ref: a branch, tag or commit (default: main)
#
# The fork (CLAY_REPO, default github.com/robknopf/clay) is nicbarker/clay plus fixes
# libsk needs, each on its own branch merged into the fork's main; sync it with
# upstream there, then run this. Clay is used only by examples/clay.c. The files are
# copied at their upstream paths, so the demo layout's own include of "../../clay.h"
# resolves unchanged. deps/clay/VERSION records the fork commit and the upstream
# commit it's based on. Review the diff, rebuild (make verify, make webcheck), then
# commit.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REPO="${CLAY_REPO:-https://github.com/robknopf/clay.git}"
UPSTREAM="https://github.com/nicbarker/clay.git"
REF="${1:-main}"
DEST="$ROOT/deps/clay"
SRC="$ROOT/build/clay-src"
FILES="clay.h LICENSE.md examples/shared-layouts/clay-video-demo.c"

if [ ! -d "$SRC/.git" ]; then
    git clone --quiet "$REPO" "$SRC"
fi
git -C "$SRC" fetch --quiet "$REPO"
git -C "$SRC" fetch --quiet "$UPSTREAM" main
UPSTREAM_HEAD="$(git -C "$SRC" rev-parse FETCH_HEAD)"
git -C "$SRC" fetch --quiet "$REPO" "$REF"
git -C "$SRC" checkout --quiet --detach FETCH_HEAD
COMMIT="$(git -C "$SRC" rev-parse HEAD)"
DATE="$(git -C "$SRC" log -1 --format=%cs HEAD)"
BASE="$(git -C "$SRC" merge-base HEAD "$UPSTREAM_HEAD")"

for name in $FILES; do
    if [ ! -f "$SRC/$name" ]; then
        echo "update_clay: $name is no longer in clay" >&2
        exit 1
    fi
    mkdir -p "$(dirname "$DEST/$name")"
    cp "$SRC/$name" "$DEST/$name"
done

{
    echo "# Vendored from libsk's fork of nicbarker/clay (zlib license, LICENSE.md): a C99 UI"
    echo "# layout library (flexbox-like; emits render commands and draws nothing itself), with"
    echo "# fixes libsk needs on top of upstream. Used only by examples/clay.c, which draws it"
    echo "# through libsk's public API; libsk itself doesn't include or link Clay"
    echo "# (docs/PLAN-ui.md). Files keep their upstream paths; update them all together with"
    echo "# tools/update_clay.sh."
    echo "clay-fork $REPO $COMMIT $DATE"
    echo "clay-upstream $UPSTREAM $BASE"
} > "$DEST/VERSION"

echo "update_clay: deps/clay now at fork $COMMIT ($DATE), upstream base $BASE"
git -C "$ROOT" status --short -- deps/clay
