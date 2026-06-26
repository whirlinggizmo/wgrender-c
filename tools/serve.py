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


class _LimitReader:
    """Wraps a file so copyfile() stops after `remaining` bytes (one Range)."""
    def __init__(self, f, remaining):
        self.f, self.remaining = f, remaining

    def read(self, n=-1):
        if self.remaining <= 0:
            return b""
        if n is None or n < 0 or n > self.remaining:
            n = self.remaining
        data = self.f.read(n)
        self.remaining -= len(data)
        return data

    def close(self):
        self.f.close()


class Handler(http.server.SimpleHTTPRequestHandler):
    def translate_path(self, path):
        path = urllib.parse.urlparse(path).path
        path = posixpath.normpath(urllib.parse.unquote(path))
        parts = [p for p in path.split("/") if p not in ("", ".", "..")]
        if parts and parts[0] == "assets":          # /assets/* -> examples/assets/*
            return os.path.join(ASSETS, *parts[1:])
        return os.path.join(SITE, *parts)            # everything else -> web/*

    @staticmethod
    def _parse_range(header, file_len):
        """Single byte range -> (start, end) inclusive, or None to fall back."""
        if not header.startswith("bytes=") or "," in header:
            return None
        first, _, last = header[len("bytes="):].partition("-")
        try:
            if first == "":                          # suffix: last N bytes
                n = int(last)
                if n <= 0:
                    return None
                start, end = max(0, file_len - n), file_len - 1
            else:
                start = int(first)
                end = min(int(last), file_len - 1) if last else file_len - 1
        except ValueError:
            return None
        if start > end or start >= file_len:
            return None
        return start, end

    def send_head(self):
        # Honor a single Range request (sokol_fetch streams via Range GETs).
        rng_header = self.headers.get("Range")
        if not rng_header:
            return super().send_head()
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            return super().send_head()
        try:
            f = open(path, "rb")
        except OSError:
            self.send_error(404, "File not found")
            return None
        try:
            fs = os.fstat(f.fileno())
            rng = self._parse_range(rng_header, fs.st_size)
            if rng is None:
                f.close()
                return super().send_head()           # malformed -> full 200
            start, end = rng
            length = end - start + 1
            self.send_response(206)
            self.send_header("Content-Type", self.guess_type(path))
            self.send_header("Content-Range", f"bytes {start}-{end}/{fs.st_size}")
            self.send_header("Content-Length", str(length))
            self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
            self.end_headers()
            f.seek(start)
            return _LimitReader(f, length)
        except Exception:
            f.close()
            raise

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")  # honest reload-on-change
        self.send_header("Accept-Ranges", "bytes")
        super().end_headers()


if __name__ == "__main__":
    print(f"libsk: http://localhost:{PORT}/  (web/ at /, examples/assets/ mounted at /assets/)")
    http.server.HTTPServer(("", PORT), Handler).serve_forever()
