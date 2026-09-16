#!/usr/bin/env bash
# Desktop smoke test (`make smoke`): run headless example binaries for a fixed
# number of frames and fail on a non-zero exit, a timeout, or error-level logs
# ([ERROR], [FATAL], sokol panics).
#
#   tools/smoke.sh <frames> <binary>...
#
# Runs from the repo root so examples find examples/assets. Frames are paced at
# 60 per second in headless builds, so 180 frames is about 3 seconds each.
set -u

cd "$(dirname "$0")/.." || exit 2

frames="${1:?usage: tools/smoke.sh <frames> <binary>...}"
shift
timeout_s=$(( frames / 60 + 20 ))
failed=0
log=$(mktemp)
trap 'rm -f "$log"' EXIT

echo "smoke: $# example(s), $frames frames each (headless)"
for bin in "$@"; do
    name=$(basename "$bin")
    start=$(date +%s.%N)
    SK_HEADLESS_FRAMES="$frames" timeout "$timeout_s" "examples/$bin" > "$log" 2>&1
    rc=$?
    secs=$(printf '%.1f' "$(echo "$(date +%s.%N) - $start" | bc)")
    problems=$(grep -E '\[(ERROR|FATAL)|\[panic\]|ABORTING' "$log")
    if [ "$rc" -eq 124 ]; then
        echo "  FAIL  $name (timed out after ${timeout_s}s)"; failed=$((failed + 1))
    elif [ "$rc" -ne 0 ]; then
        echo "  FAIL  $name (exit $rc, ${secs}s)"; failed=$((failed + 1))
    elif [ -n "$problems" ]; then
        echo "  FAIL  $name (error logs, ${secs}s)"; failed=$((failed + 1))
    else
        echo "  ok    $name (${secs}s)"
        continue
    fi
    printf '%s\n' "${problems:-$(tail -n 5 "$log")}" | sed 's/^/          /'
done

if [ "$failed" -gt 0 ]; then
    echo "FAIL: $failed of $# example(s)"
    exit 1
fi
echo "PASS: $# example(s)"
