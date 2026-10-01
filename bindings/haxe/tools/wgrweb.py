"""The browser checks' shared pieces: this repository's tools/weblib.py (the browser,
DevTools and process handling), the wgrender a check runs against, and the command
that serves a site with that wgrender's assets.

    from wgrweb import W, serve_command, weblib

W is the wgrender tools/wgrpath.py finds ($WGRENDER_DIR, a sibling checkout, or the
submodule), so a check here always drives the same wgrender the build used.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from wgrpath import find  # noqa: E402
import weblib  # noqa: E402,F401

W = find(argv=[])


def serve_command(port, site, *flags):
    """tools/serve.py for SITE on PORT, W's examples/assets mounted at /assets."""
    return [weblib.PYTHON, HERE / 'serve.py', port, site, '--assets', W / 'examples/assets', *flags]
