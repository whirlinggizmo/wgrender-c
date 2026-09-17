#!/bin/sh
# Download the loading benchmark's models (glTF Sample Assets, ~100 MB, not in
# the repo) into examples/assets/bench/. Sponza: CC BY 4.0 (Crytek, Frank Meinl);
# FlightHelmet: CC0. See https://github.com/KhronosGroup/glTF-Sample-Assets.
set -eu
DEST="$(dirname "$0")/../../examples/assets/bench"
API=https://api.github.com/repos/KhronosGroup/glTF-Sample-Assets/contents/Models
for model in Sponza FlightHelmet; do
    if [ -f "$DEST/$model/.complete" ]; then
        echo "bench: $model already downloaded"
        continue
    fi
    mkdir -p "$DEST/$model"
    echo "bench: downloading $model"
    curl -sSfL --retry 3 --max-time 60 "$API/$model/glTF" |
        python3 -c 'import sys, json; [print(e["download_url"]) for e in json.load(sys.stdin) if e["type"] == "file"]' |
        (cd "$DEST/$model" && xargs -P 8 -n 1 curl -sSfLO --retry 3 --max-time 300)
    touch "$DEST/$model/.complete"
done
