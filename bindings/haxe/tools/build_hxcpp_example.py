#!/usr/bin/env python3
"""An example built all-in-one through hxcpp for the web: the Haxe program and
wgrender in one wasm, the way examples/simple-hxcpp is, but for any example here.

    tools/build_hxcpp_example.py <example>        examples/<example>/out/wasm32/release-hxcpp/site/<name>.js/.wasm

The examples are guests: on the web they normally run as JS against a wasm host. This
builds the same source the other way, through hxcpp, which is what a desktop build
does, only targeting Emscripten. It exists for tools/run_benchmarks.py, to measure the
Haxe runtime and its garbage collector inside the wasm on the same scene the JS guest
runs; it is not how an example is meant to ship to the web.

It is examples/simple-hxcpp/build.py's web build, generalised: wgrender compiled in
from its sources by emcc with its own web flags (project/Build.xml, as every build of
the binding), the guest glue compiled in without its main (hxcpp brings one), and
single-threaded (hxcpp's emscripten target has none). The entry class is the
example's own, read from its build.desktop.hxml. BACKEND=webgpu and WEB_DEBUG=1 as
for wgrender's web builds; the work is in build/wasm32-<variant>/.
"""
import os
import pathlib
import shutil
import subprocess
import sys

LIB = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(LIB / 'tools'))
from wgrpath import WGRENDER, web_variant  # noqa: E402
from guestbuild import check_library, finish_site, main_class  # noqa: E402

HAXE = os.environ.get('HAXE', 'haxe')


def run(cmd, **kw):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    name = sys.argv[1]
    example = LIB / 'examples' / name
    entry = main_class(example, 'build.desktop.hxml')

    # the web has two toolchains here (a JS guest, and this), so the variant names it:
    # release-hxcpp, release-webgpu-hxcpp, debug-hxcpp (never threads)
    backend = os.environ.get('BACKEND') or 'webgl2'
    debug = (os.environ.get('WEB_DEBUG') or '0') == '1'
    variant = web_variant(hxcpp=True)
    build = example / 'build' / f'wasm32-{variant}'
    build.mkdir(parents=True, exist_ok=True)

    check_library()  # project/Build.xml is found through haxelib: this copy's
    run([HAXE, '-cp', 'src', '-lib', 'wgrender-hx', '--main', entry,
         '--cpp', build / 'cpp', '-D', 'emscripten', '-D', f'HAXE_OUTPUT_FILE={name}',
         *(['-D', 'wgr-webgpu'] if backend == 'webgpu' else []),
         *(['--debug'] if debug else []), '--macro', 'wgr.macros.NativeOut.toolchain()',
         '-dce', 'full', '-D', 'analyzer-optimize'],
        cwd=example)

    site = example / 'out/wasm32' / variant / 'site'
    site.mkdir(parents=True, exist_ok=True)
    for f in (f'{name}.js', f'{name}.wasm'):
        shutil.copy2(build / 'cpp' / f, site / f)
    finish_site(site, build, name, f'examples/{name}/src/{entry.replace(".", "/")}.hx')
    print(f'built {site}')


if __name__ == '__main__':
    import cli  # noqa: E402  (tools/cli.py: --help, and no argument it doesn't take)
    cli.parse(__doc__, (), positional=1)
    main()
