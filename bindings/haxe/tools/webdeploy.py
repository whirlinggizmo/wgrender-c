#!/usr/bin/env python3
"""Taken from wgrender's tools/webdeploy.py, and this repository's own now.

Finish a web build's site (out/web/<variant>/): the page and the manifest.

    tools/webdeploy.py SITE SHELL

Writes SITE/index.html from SHELL with every program's version written in: a short
hash of its .js and .wasm, so the page loads `name.js?v=<hash>` and
`name.wasm?v=<hash>`. A host can then let browsers keep those for good (they change
name when they change; tools/serve.py --cache does), and a returning visit fetches no
code at all. The versions are in the page itself, not a file beside it: that would be
one more round trip before the code could start downloading.

Also writes SITE/examples.json, the programs built (the page's switcher and the web
tools read it). Stdlib only.
"""
import hashlib
import json
import os
import sys

MARK = "/*wgr:versions*/{}"


def version(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()[:12]


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: webdeploy.py SITE SHELL")
    site, shell = sys.argv[1], sys.argv[2]
    names = sorted(f[:-3] for f in os.listdir(site) if f.endswith(".js"))
    versions = {}
    for name in names:
        entry = {"js": version(os.path.join(site, name + ".js"))}
        wasm = os.path.join(site, name + ".wasm")
        if os.path.exists(wasm):
            entry["wasm"] = version(wasm)
        versions[name] = entry
    with open(shell, encoding="utf-8") as f:
        page = f.read()
    if MARK not in page:
        sys.exit(f"webdeploy.py: {shell} has no {MARK} to fill in")
    page = page.replace(MARK, json.dumps(versions, separators=(",", ":")))
    with open(os.path.join(site, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)
    with open(os.path.join(site, "examples.json"), "w", encoding="utf-8") as f:
        f.write(json.dumps(names, separators=(",", ":")) + "\n")


if __name__ == "__main__":
    main()
