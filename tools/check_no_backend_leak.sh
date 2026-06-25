#!/usr/bin/env bash
# Enforce the "no backend leakage" invariant:
#   - Public headers (include/) and example sources (examples/) must not depend
#     on the sokol backend: no sokol #includes and no sokol API identifiers.
#   - The prose word "sokol" in comments is allowed; only API symbols are
#     flagged (sapp_/sgl_/sdtx_/sglue_/stm_/saudio_/sfetch_/sg_<lc>, SOKOL_/SAPP_).
#
# Exit non-zero on any violation. Run from the repo root: tools/check_no_backend_leak.sh
set -u

cd "$(dirname "$0")/.." || exit 2

# sokol API token pattern (identifiers only — not the bare word "sokol")
PATTERN='\b(sapp_|sgl_|sdtx_|sglue_|stm_|saudio_|sfetch_|sg_[a-z]|sg_[A-Z])|\b(SOKOL_|SAPP_)|#[[:space:]]*include[[:space:]]*[<"]sokol'

status=0

scan() {
    local dir="$1" glob="$2"
    local hits
    hits=$(grep -rnE "$PATTERN" $dir/$glob 2>/dev/null)
    if [ -n "$hits" ]; then
        echo "FAIL: backend (sokol) symbols leaked into $dir/:"
        echo "$hits"
        status=1
    else
        echo "ok: $dir/ is backend-free"
    fi
}

scan include "*.h"
scan examples "*.c"

if [ "$status" -eq 0 ]; then
    echo "PASS: no backend leakage"
fi
exit "$status"
