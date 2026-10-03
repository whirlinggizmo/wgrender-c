"""The browser checks' shared pieces: wgrender's tools/browser.py (the browser, DevTools
and process handling), the wgrender a check runs against, and the command that serves a
site with that wgrender's assets (wgrender's tools/serve_site.py).

    from wgrweb import W, serve_command  (and wgrender's tools/browser.py, on the path)

W is the wgrender this binding lives in (tools/wgrpath.py), so a check here always
drives the same wgrender the build used.
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from wgrpath import WGRENDER  # noqa: E402  (and wgrender's tools/ on the path)
from browser import PYTHON  # noqa: E402  (wgrender's tools/browser.py)

W = WGRENDER


def serve_command(port, site, *flags):
    """tools/serve_site.py for SITE on PORT, W's examples/assets mounted at /assets."""
    return [PYTHON, W / 'tools/serve_site.py', port, site, '--assets', W / 'examples/assets', *flags]
