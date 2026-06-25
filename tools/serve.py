#!/usr/bin/env python3
"""Dev server for the libsk web build (stdlib only; cross-platform).

Serves the built site (examples/build/web/) at / and *mounts* the shared asset
tree (examples/assets/) at /assets/ — so assets are never copied or symlinked into
the site. Single source of truth, works on Windows/macOS/Linux. This mirrors the
web asset host "/assets/" (the same logical path the desktop fs resolves locally).

    python3 tools/serve.py [port]      # default 8000
"""
import http.server
import os
import posixpath
import sys
import urllib.parse

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE   = os.path.join(ROOT, "examples", "build", "web")
ASSETS = os.path.join(ROOT, "examples", "assets")
PORT   = int(sys.argv[1]) if len(sys.argv) > 1 else 8000


class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        path = urllib.parse.urlparse(path).path
        path = posixpath.normpath(urllib.parse.unquote(path))
        parts = [p for p in path.split("/") if p not in ("", ".", "..")]
        if parts and parts[0] == "assets":          # /assets/* -> examples/assets/*
            return os.path.join(ASSETS, *parts[1:])
        return os.path.join(SITE, *parts)            # everything else -> web/*

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")  # honest reload-on-change
        super().end_headers()


if __name__ == "__main__":
    print(f"libsk: http://localhost:{PORT}/  (web/ at /, examples/assets/ mounted at /assets/)")
    http.server.HTTPServer(("", PORT), Handler).serve_forever()
