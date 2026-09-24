"""Emscripten's own tools, found the way the web presets find emcc: the emsdk that
$EM_CONFIG or $EMSDK names, else the one whose emcc is on PATH.

    import emsdk
    emsdk.node()      # the Node Emscripten runs on, or None without an emsdk

The web tools (webcheck, webstart, the benchmark harness) run on Emscripten's Node
rather than whatever `node` is on PATH: a web build needs emsdk anyway, and so a
browser check needs nothing more, where a PATH's Node may be missing or too old.
"""
import os
import shutil
from pathlib import Path


def config():
    """emsdk's .emscripten, or None."""
    if os.environ.get('EM_CONFIG') and Path(os.environ['EM_CONFIG']).is_file():
        return Path(os.environ['EM_CONFIG'])
    if os.environ.get('EMSDK') and (Path(os.environ['EMSDK']) / '.emscripten').is_file():
        return Path(os.environ['EMSDK']) / '.emscripten'
    emcc = shutil.which('emcc')  # <emsdk>/upstream/emscripten/emcc[.bat]
    if emcc:
        candidate = Path(emcc).resolve().parents[2] / '.emscripten'
        if candidate.is_file():
            return candidate
    return None


def node():
    """The Node Emscripten uses (its config's NODE_JS), or None."""
    path = config()
    if path is None:
        return None
    # The config is Python that computes its paths from EM_CONFIG; emcc runs it the same way.
    saved = os.environ.get('EM_CONFIG')
    os.environ['EM_CONFIG'] = str(path)
    try:
        settings = {}
        exec(path.read_text(), settings)
    finally:
        if saved is None:
            del os.environ['EM_CONFIG']
        else:
            os.environ['EM_CONFIG'] = saved
    found = settings.get('NODE_JS')
    if isinstance(found, list):  # NODE_JS may be a command with arguments
        found = found[0] if found else None
    return found if found and Path(found).is_file() else None
