#!/usr/bin/env python3
"""Run a web tool that is still JS (the benchmark harness) on Emscripten's Node.

    tools/node.py tools/bench/bench.mjs ...

The web tools need Node 22 or newer, which emsdk brings: a web build needs emsdk, so the
tools that check one run on its Node rather than on whatever `node` is on PATH (missing,
or too old, on plenty of machines). The tools start their helpers (tools/serve.py,
tools/webwatch.py) on the Python running this.
"""
import os
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import emsdk  # noqa: E402


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    node = emsdk.node()
    if node is None:
        sys.exit('node.py: no Emscripten (emsdk) found: set EMSDK, or put emcc on PATH (emsdk_env)')
    env = dict(os.environ, WGR_PYTHON=sys.executable)
    sys.exit(subprocess.run([node, *sys.argv[1:]], env=env).returncode)


if __name__ == '__main__':
    main()
