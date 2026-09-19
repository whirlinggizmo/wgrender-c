#!/usr/bin/env bash
# Run a Windows program (a WINDOWS=1 build) under Wine: the unit tests and the smoke
# run of the Windows builds use it.
#
#   tools/wine.sh program.exe [args...]
#
# Which Wine: $WINE if set, else wine64 or wine on PATH, else the newest Proton in a
# Steam library (its files/bin/wine; Proton is Valve's Wine, installed from Steam's
# Library > Tools). The Wine prefix (its fake C: drive and registry) is build/wine
# unless WINEPREFIX says otherwise; it's made on first use, which takes a few
# seconds. Wine's own debug output is off unless WINEDEBUG is set. Exits with the
# program's exit code, or 127 when there's no Wine.
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"

find_wine() {
    if [ -n "${WINE:-}" ]; then
        echo "$WINE"
        return
    fi
    for name in wine64 wine; do
        if command -v "$name" >/dev/null 2>&1; then
            command -v "$name"
            return
        fi
    done
    local libraries=("$HOME/.local/share/Steam" "$HOME/.steam/steam")
    local vdf="$HOME/.local/share/Steam/steamapps/libraryfolders.vdf"
    if [ -f "$vdf" ]; then
        while IFS= read -r path; do libraries+=("$path"); done < <(sed -n 's/.*"path"[[:space:]]*"\([^"]*\)".*/\1/p' "$vdf")
    fi
    local best=""
    for library in "${libraries[@]}"; do
        for proton in "$library"/steamapps/common/Proton*; do
            [ -x "$proton/files/bin/wine" ] && best="$proton/files/bin/wine"$'\n'"$best"
        done
    done
    # newest version first ("Proton 11.0" after "Proton 9.0")
    printf '%s' "$best" | sed '/^$/d' | sort -V -r | head -n 1
}

wine="$(find_wine)"
if [ -z "$wine" ]; then
    echo "wine.sh: no Wine found (install wine64, or Proton from Steam's Library > Tools, or set WINE)" >&2
    exit 127
fi
export WINEPREFIX="${WINEPREFIX:-$root/build/wine}"
export WINEDEBUG="${WINEDEBUG:--all}"
mkdir -p "$WINEPREFIX"
exec "$wine" "$@"
