#!/usr/bin/env python3
"""An example built all-in-one through hxcpp for the web: the Haxe program and
wgrender in one wasm, the way examples/simple-hxcpp is, but for any example here.

    tools/hxcppweb.py <example>        examples/<example>/out/web/hxcpp-webgl2-nothreads/<name>.js/.wasm

The examples are guests: on the web they normally run as JS against a wasm host. This
builds the same source the other way, through hxcpp, which is what a desktop build
does, only targeting Emscripten. It exists for tools/benchmarks.py, to measure the
Haxe runtime and its garbage collector inside the wasm on the same scene the JS guest
runs; it is not how an example is meant to ship to the web.

It is examples/simple-hxcpp/build.py's web build, generalised: wgrender compiled in
from its sources by emcc with its own web flags (project/Build.xml, as every build of
the binding), the guest glue compiled in without its main (hxcpp brings one), and
single-threaded (hxcpp's emscripten target has none). The entry class is the
example's own, read from its build.desktop.hxml. BACKEND=webgpu and WEB_DEBUG=1 as
for wgrender's web builds; the work is in build/web/hxcpp-<variant>/.
"""
import os
import pathlib
import re
import shutil
import subprocess
import sys

LIB = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LIB / 'tools'))
from wgrpath import find  # noqa: E402
from guestbuild import check_library  # noqa: E402

WGRENDER = find(argv=[])
HAXE = os.environ.get('HAXE', 'haxe')


def run(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def finish_site(site, work, name, source):
    """The page for an all-in-one build in SITE: web/index.html opening NAME, its source
    link to SOURCE (a path in this repository), finished by tools/webdeploy.py with
    the versioned file names and examples.json. The page is written in WORK first."""
    page = (LIB / 'web/index.html').read_text(encoding='utf-8')
    for mark in ('/*wgr:first*/"simple-hxcpp"', '/*wgr:source*/"'):
        if mark not in page:
            sys.exit(f'{LIB}/web/index.html: no {mark} to fill in')
    page = page.replace('/*wgr:first*/"simple-hxcpp"', f'/*wgr:first*/"{name}"')
    page = re.sub(r'/\*wgr:source\*/"[^"]*"',
                  f'/*wgr:source*/"https://github.com/whirlinggizmo/wgrender-hx/blob/main/{source}"', page)
    shell = work / 'index.html'
    shell.write_text(page, encoding='utf-8')
    run([sys.executable, LIB / 'tools/webdeploy.py', site, shell])
    shell.unlink()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    name = sys.argv[1]
    example = LIB / 'examples' / name
    desktop = (example / 'build.desktop.hxml').read_text(encoding='utf-8')
    entry = re.search(r'^--main\s+(\S+)', desktop, re.M)
    if not entry:
        sys.exit(f'{example}/build.desktop.hxml names no --main')

    # the web has two toolchains here (a JS guest, and this), so the variant names it
    backend = os.environ.get('BACKEND') or 'webgl2'
    debug = (os.environ.get('WEB_DEBUG') or '0') == '1'
    variant = f'hxcpp-{backend}-nothreads' + ('-debug' if debug else '')
    build = example / 'build/web' / variant
    build.mkdir(parents=True, exist_ok=True)

    check_library()  # project/Build.xml is found through haxelib: this copy's
    run([HAXE, '-cp', 'src', '-lib', 'wgrender-hx', '--main', entry.group(1),
         '--cpp', build / 'cpp', '-D', 'emscripten', '-D', f'HAXE_OUTPUT_FILE={name}',
         '-D', f'WGRENDER_DIR={WGRENDER}', *(['-D', 'wgr-webgpu'] if backend == 'webgpu' else []),
         *(['--debug'] if debug else []), '--macro', 'wgr.macros.NativeOut.toolchain()',
         '-dce', 'full', '-D', 'analyzer-optimize'],
        cwd=example)

    site = example / 'out/web' / variant
    site.mkdir(parents=True, exist_ok=True)
    for f in (f'{name}.js', f'{name}.wasm'):
        shutil.copy2(build / 'cpp' / f, site / f)
    finish_site(site, build, name, f'examples/{name}/src/{entry.group(1).replace(".", "/")}.hx')
    print(f'built {site}')


if __name__ == '__main__':
    main()
