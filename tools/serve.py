#!/usr/bin/env python3
"""Dev server for the libsk web build (stdlib only; cross-platform).

Serves a built site (examples/build/webgl2/ or webgpu/) at / and *mounts* the shared asset
tree (examples/assets/) at /assets/ — so assets are never copied or symlinked into
the site. Single source of truth, works on Windows/macOS/Linux. This mirrors the
web asset host "/assets/" (the same logical path the desktop fs resolves locally).

    python3 tools/serve.py [port] [site] [--tls CERT KEY]   # default 8000, examples/build/webgl2

--tls serves HTTPS with that certificate and key (PEM), e.g. a locally trusted dev
certificate, so another device on the LAN (a phone) gets a secure page: threaded
builds need one for SharedArrayBuffer. localhost is secure without it.
"""
import http.server
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

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE   = os.path.abspath(ARGS[1]) if len(ARGS) > 1 else os.path.join(ROOT, "examples", "build", "webgl2")
ASSETS = os.path.join(ROOT, "examples", "assets")
PORT   = int(ARGS[0]) if len(ARGS) > 0 else 8000


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
    print(f"libsk: {scheme}://localhost:{PORT}/  ({SITE} at /, examples/assets/ mounted at /assets/)",
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
