#!/usr/bin/env python3
"""Dev server for a web build (stdlib only; cross-platform). Taken from wgrender's
tools/serve.py, and this repository's own now: the only change is --assets, since
the asset tree is wgrender's and not beside this file.

Serves a built site at / and *mounts* the shared asset tree (wgrender's
examples/assets/) at /assets/ — so assets are never copied or symlinked into the
site. Single source of truth, works on Windows/macOS/Linux. This mirrors the web
asset host "/assets/" (the same logical path the desktop fs resolves locally).

    python3 tools/serve.py [port] SITE --assets DIR [--tls CERT KEY] [--cache] [--gzip]
                                                            # default port 8000

--tls serves HTTPS with that certificate and key (PEM), e.g. a locally trusted dev
certificate, so another device on the LAN (a phone) gets a secure page: threaded
builds need one for SharedArrayBuffer. localhost is secure without it.

By default nothing is cached (no-store: a reload always gets the latest build).
--cache and --gzip serve the way a host should, for measuring startup
(tools/webstart.py): --cache lets the browser keep versioned files (name?v=<hash>,
as the page loads code: tools/webdeploy.py) for good (immutable, a year) and
revalidate the rest every visit (no-cache, answered 304 while unchanged); --gzip
compresses the page, JS, wasm and JSON (not Range requests: assets stream through
them).
"""
import email.utils
import gzip
import http.server
import io
import os
import posixpath
import ssl
import sys
import urllib.parse

ARGS = sys.argv[1:]
TLS = None
if "--tls" in ARGS:
    i = ARGS.index("--tls")
    if len(ARGS) < i + 3:
        sys.exit("serve.py: --tls needs CERT and KEY")
    TLS = (ARGS[i + 1], ARGS[i + 2])
    del ARGS[i:i + 3]
ASSETS = None
if "--assets" in ARGS:
    i = ARGS.index("--assets")
    if len(ARGS) < i + 2:
        sys.exit("serve.py: --assets needs DIR")
    ASSETS = os.path.abspath(ARGS[i + 1])
    del ARGS[i:i + 2]
CACHE = "--cache" in ARGS
GZIP = "--gzip" in ARGS
ARGS = [a for a in ARGS if a not in ("--cache", "--gzip")]
# What a real static host compresses, which is the point of --gzip: GitHub Pages
# gzips .glb, .gltf and .ttf as well as the obvious text types (not .png, already
# compressed). Keeping this list short hid a bug for a while -- the asset fetch broke
# on exactly the types our own "serve as a host would" mode never compressed.
GZIP_TYPES = (".html", ".js", ".wasm", ".json", ".css", ".txt", ".glb", ".gltf", ".ttf")

if len(ARGS) < 2 or ASSETS is None:
    sys.exit("serve.py: [port] SITE --assets DIR (wgrender's examples/assets)")
SITE   = os.path.abspath(ARGS[1])
PORT   = int(ARGS[0])


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
    # The web's types, whatever this machine says: Python's mimetypes reads Windows'
    # registry, where .js is often text/plain, and a browser refuses to run a module
    # script served as that (a guest's boot.js), without a console message a page sees.
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".js": "text/javascript", ".mjs": "text/javascript", ".wasm": "application/wasm",
        ".json": "application/json", ".html": "text/html", ".css": "text/css",
        ".png": "image/png", ".jpg": "image/jpeg", ".svg": "image/svg+xml",
        ".gltf": "model/gltf+json", ".glb": "model/gltf-binary", ".ttf": "font/ttf",
    }

    def translate_path(self, path):
        path = urllib.parse.urlparse(path).path
        path = posixpath.normpath(urllib.parse.unquote(path))
        parts = [p for p in path.split("/") if p not in ("", ".", "..")]
        if parts and parts[0] == "assets":          # /assets/* -> ASSETS/*
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

    _gzipped = {}  # (path, mtime) -> compressed bytes

    def _send_gzipped(self, path):
        """The file compressed (304 while unchanged), or None to serve it as is."""
        if (not GZIP or not path.endswith(GZIP_TYPES) or
                "gzip" not in self.headers.get("Accept-Encoding", "")):
            return None
        try:
            fs = os.stat(path)
        except OSError:
            return None
        since = self.headers.get("If-Modified-Since")
        if since and CACHE:
            try:
                if int(fs.st_mtime) <= email.utils.parsedate_to_datetime(since).timestamp():
                    self.send_response(304)
                    self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
                    self.end_headers()
                    return io.BytesIO(b"")
            except (TypeError, ValueError):
                pass
        key = (path, fs.st_mtime)
        if key not in Handler._gzipped:
            with open(path, "rb") as f:
                Handler._gzipped[key] = gzip.compress(f.read(), 6)
        body = Handler._gzipped[key]
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Encoding", "gzip")
        self.send_header("Vary", "Accept-Encoding")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Last-Modified", self.date_time_string(fs.st_mtime))
        self.end_headers()
        return io.BytesIO(body)

    def send_head(self):
        # Honor a single Range request (sokol_fetch streams via Range GETs).
        rng_header = self.headers.get("Range")
        if not rng_header:
            path = self.translate_path(self.path)
            if not os.path.isdir(path):
                zipped = self._send_gzipped(path)
                if zipped is not None:
                    return zipped
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
        # default: honest reload-on-change. --cache: versioned files (?v=<hash>) never
        # change, keep them; revalidate the rest every load
        if not CACHE:
            self.send_header("Cache-Control", "no-store")
        elif "v=" in urllib.parse.urlparse(self.path).query:
            self.send_header("Cache-Control", "public, max-age=31536000, immutable")
        else:
            self.send_header("Cache-Control", "no-cache")
        self.send_header("Accept-Ranges", "bytes")
        # cross-origin isolation: threaded builds need SharedArrayBuffer
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()


if __name__ == "__main__":
    server = http.server.ThreadingHTTPServer(("", PORT), Handler)
    scheme = "http"
    if TLS is not None:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certfile=os.path.expanduser(TLS[0]), keyfile=os.path.expanduser(TLS[1]))
        server.socket = context.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    print(f"serving {scheme}://localhost:{PORT}/  ({SITE} at /, {ASSETS} mounted at /assets/)",
          flush=True)
    if TLS is not None:
        import socket
        try:  # the address other devices reach this machine at (no packet is sent)
            probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            probe.connect(("192.0.2.1", 9))
            print(f"       on the LAN: https://{probe.getsockname()[0]}:{PORT}/", flush=True)
            probe.close()
        except OSError:
            pass
    server.serve_forever()
