#!/usr/bin/env bash
# librl -> libwgrender API parity report (`make parity`).
#
# Reads the public functions (and function-like macros) from both libraries'
# include/*.h, with comments stripped, and checks them against tools/parity.map:
#
# Functional parity, not a 1:1 API: the map records how each librl function's
# capability is covered, dropped or still open (see the header of parity.map).
#
#   - rl_foo with a matching wgr_foo counts as covered (no map entry needed)
#   - every other librl function must have a map entry     (else FAIL: unmapped)
#   - every map entry must name a current librl function    (else FAIL: stale)
#   - a `ported` entry's target must exist in libwgrender         (else FAIL: missing)
#
# Prints counts and the todo list. Exits 0 unless a check fails; with --strict,
# remaining todos also fail.
#
#   tools/parity.sh [--strict]          LIBRL_DIR defaults to ../librl
set -u

cd "$(dirname "$0")/.." || exit 2

strict=0
[ "${1:-}" = "--strict" ] && strict=1

MAP=tools/parity.map

# Where librl is. LIBRL_DIR wins; otherwise a checkout beside this repo, then beside
# the directory holding it (repos are often grouped by owner), then the read-only copy
# in reference/librl -- the one the map was written against, so a bare checkout can
# print the report.
if [ -z "${LIBRL_DIR:-}" ]; then
    for candidate in ../librl ../../*/librl reference/librl; do
        if [ -d "$candidate/include" ]; then
            LIBRL_DIR="$candidate"
            break
        fi
    done
fi
LIBRL_DIR="${LIBRL_DIR:-../librl}"

if [ ! -d "$LIBRL_DIR/include" ]; then
    echo "parity: librl not found at $LIBRL_DIR (set LIBRL_DIR=/path/to/librl)" >&2
    exit 2
fi

# list_api <dir> <prefix>: public function / function-like macro names, sorted.
list_api() {
    local f
    for f in "$1"/include/*.h; do
        perl -0777 -ne '
            my $p = "'"$2"'";
            s{/\*.*?\*/}{ }gs; s{//[^\n]*}{}g;
            while (/^\s*#\s*define\s+(${p}_\w+)\(/gm) { print "$1\n" }
            s{^\s*#[^\n]*(?:\\\n[^\n]*)*}{}gm;
            while (/(?<![\w(])\**\s*(${p}_\w+)\s*\(/g) { print "$1\n" }
        ' "$f"
    done | sort -u
}

rl_api=$(list_api "$LIBRL_DIR" rl)
wgr_api=$(list_api . wgr)

has_wgr() { grep -qxF "$1" <<<"$wgr_api"; }
has_rl() { grep -qxF "$1" <<<"$rl_api"; }

status=0
declare -A entry_status entry_arg
map_errors=""

while read -r name kind arg rest; do
    case "$name" in ''|'#'*) continue ;; esac
    if [ -n "${entry_status[$name]+x}" ]; then
        map_errors+="  duplicate entry: $name"$'\n'
        continue
    fi
    case "$kind" in
        ported|dropped|todo) ;;
        *) map_errors+="  bad status '$kind' for $name (ported|dropped|todo)"$'\n'; continue ;;
    esac
    entry_status[$name]=$kind
    entry_arg[$name]="${arg:-}${rest:+ $rest}"
done < "$MAP"

auto=0; ported=0; dropped=0; todo=0
unmapped=""; missing=""; todo_list=""

while read -r rl; do
    [ -z "$rl" ] && continue
    wgr="wgr_${rl#rl_}"
    kind="${entry_status[$rl]:-}"
    if [ -z "$kind" ]; then
        if has_wgr "$wgr"; then auto=$((auto + 1)); else unmapped+="  $rl"$'\n'; fi
        continue
    fi
    case "$kind" in
        ported)
            target="${entry_arg[$rl]%% *}"
            if [ -z "$target" ] || ! has_wgr "$target"; then
                missing+="  $rl -> ${target:-<none>}"$'\n'
            else
                ported=$((ported + 1))
            fi ;;
        dropped) dropped=$((dropped + 1)) ;;
        todo)
            todo=$((todo + 1))
            line=$(printf '  %-40s %s' "$rl" "${entry_arg[$rl]}")
            todo_list+="${line%"${line##*[! ]}"}"$'\n' ;;
    esac
done <<<"$rl_api"

stale=""
for name in "${!entry_status[@]}"; do
    has_rl "$name" || stale+="  $name"$'\n'
done

total=$(grep -c . <<<"$rl_api")
done_count=$((auto + ported))
echo "librl public API: $total functions ($LIBRL_DIR)"
echo "  covered: $done_count ($auto same name, $ported by a different libwgrender function)"
echo "  dropped: $dropped (on purpose)"
echo "  todo:    $todo"
if [ $((total - dropped)) -gt 0 ]; then
    echo "  parity:  $((done_count * 100 / (total - dropped)))% of librl functions not dropped (a rough measure; the goal is functional parity)"
fi

if [ -n "$todo_list" ]; then
    echo
    echo "todo:"
    printf '%s' "$todo_list"
fi

report_fail() {
    echo
    echo "FAIL: $1"
    printf '%s' "$2" | sort
    status=1
}
[ -n "$map_errors" ] && report_fail "malformed $MAP entries:" "$map_errors"
[ -n "$unmapped" ]   && report_fail "librl functions with no wgr_ match and no entry in $MAP:" "$unmapped"
[ -n "$stale" ]      && report_fail "$MAP entries for functions librl no longer has:" "$stale"
[ -n "$missing" ]    && report_fail "'ported' entries whose libwgrender function doesn't exist:" "$missing"

if [ "$strict" -eq 1 ] && [ "$todo" -gt 0 ]; then
    echo
    echo "FAIL: --strict and $todo todo entries remain"
    status=1
fi

[ "$status" -eq 0 ] && echo && echo "PASS: parity map is consistent"
exit "$status"
