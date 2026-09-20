#!/usr/bin/env bash
# Enforce libwgrender naming conventions (see AGENTS.md § Naming):
#
#   1. Struct types are wgr_<noun>_t — the noun carries the layer (resource vs
#      object). No _data_t / _instance_t suffixes.
#   2. A local/param holding a raw instance pointer resolved from a handle
#      (`<type> *NAME = ... resolve...(`) must be named <noun>_ptr, so the
#      pointer path stays visually distinct from the handle path.
#   3. No wgr_ name is declared in both include/ and src/internal/. Public and
#      internal share one namespace, so a name means one thing either way; the
#      compiler catches a clash between two functions, but a function-like macro
#      in a public header would shadow an internal function silently.
#
# Exit non-zero on any violation. Run from anywhere: tools/check_naming.sh
set -u

cd "$(dirname "$0")/.." || exit 2

status=0

# (1) banned struct type suffixes in our own sources
banned=$(grep -rnE '\}[[:space:]]*wgr_[a-z0-9_]+_(data|instance)_t[[:space:]]*;' src include 2>/dev/null)
if [ -n "$banned" ]; then
    echo "FAIL: struct types must be wgr_<noun>_t (no _data_t / _instance_t):"
    echo "$banned"
    status=1
else
    echo "ok: no _data_t / _instance_t struct types"
fi

# (2) resolved instance pointers must end in _ptr
#     flag declarations `<type> *NAME = ... resolve[_x](` whose NAME lacks _ptr
resolved=$(grep -rnE '\*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*=.*\bresolve(_[a-z]+)?\(' src/*.c 2>/dev/null)
bad_ptr=$(printf '%s\n' "$resolved" | grep -vE '\*[A-Za-z_][A-Za-z0-9_]*_ptr[[:space:]]*=')
if [ -n "$resolved" ] && [ -n "$bad_ptr" ]; then
    echo "FAIL: a pointer resolved from a handle must be named <noun>_ptr:"
    echo "$bad_ptr"
    status=1
else
    echo "ok: resolved instance pointers use _ptr"
fi

# (3) public API is handle-only: no byte buffers, no *_from_memory in include/
#     (allowed pointers in public headers: `const char *`, `void *user_data`,
#      and function-pointer typedefs — so we flag the byte-buffer leak directly)
api=$(grep -rnE '_from_memory|\bunsigned char[[:space:]]*\*' include 2>/dev/null)
if [ -n "$api" ]; then
    echo "FAIL: public API must be handle-only (no *_from_memory / raw byte buffers):"
    echo "$api"
    status=1
else
    echo "ok: public API is handle-only (no byte buffers / *_from_memory)"
fi

# (4) one namespace: a wgr_ name belongs to include/ or to src/internal/, not both
names_in() {
    # function declarations and function-like macros, by name. Comments go first:
    # internal headers name public functions in prose all the time.
    perl -0777 -ne 's{/\*.*?\*/}{ }gs; s{//[^\n]*}{}g; print' "$1"/*.h 2>/dev/null |
        grep -oE '(^|[^a-z0-9_])wgr_[a-z0-9_]+[[:space:]]*\(' |
        grep -oE 'wgr_[a-z0-9_]+' | sort -u
}
both=$(comm -12 <(names_in include) <(names_in src/internal))
if [ -n "$both" ]; then
    echo "FAIL: these wgr_ names are declared in both include/ and src/internal/:"
    echo "$both" | sed 's/^/  /'
    echo "  (one namespace: rename one of them, or promote it to include/ and drop the internal one)"
    status=1
else
    echo "ok: no wgr_ name is declared both publicly and internally"
fi

if [ "$status" -eq 0 ]; then
    echo "PASS: naming conventions"
fi
exit "$status"
