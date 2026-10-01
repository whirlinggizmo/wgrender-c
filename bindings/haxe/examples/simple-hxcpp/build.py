#!/usr/bin/env python3
"""Build the wgrender simple example in Haxe (Haxe -> hxcpp -> C++ -> native/wasm).

    ./build.py desktop     out/<os>/<variant>/simple (builds wgrender's native lib first)
    ./build.py web         out/web/webgl2-nothreads/: simple.js/.wasm + the library's page (web/index.html)
    ./build.py all         both

    ./build.py serve       serve the web build on http://localhost:8000 (the library's tools/serve.py:
                           COOP/COEP headers for threads, wgrender's examples/assets at /assets,
                           gzip-compressed responses)
    ./build.py sizes       wasm/JS sizes of the web build, raw and gzipped
    ./build.py compare     the same, next to wgrender's C example and the Nim and Beef ports
    ./build.py clean       remove out/ (what the builds made) and build/ (their work)

The binding's own checks are the library's, not this example's: run test/check.py
at the library root.

The wgr binding is the wgrender-hx haxelib (haxelib dev wgrender-hx <path>), shared
with the guest ports; this project only carries the example and its build.

wgrender is compiled in from its sources, as every build of the binding is
(project/Build.xml in wgrender-hx, by whichever compiler hxcpp uses); this passes
-D WGRENDER_DIR so it is the wgrender this checkout works against. The work is in
build/<platform>/<variant>/ (build/linux/release, build/web/webgl2-nothreads, ...: the
wg* layout, whirlinggizmo/.github CONVENTIONS.md), where wgr.macros.NativeOut puts
hxcpp's output. `haxe build.hxml` and `haxe web.hxml` work on their own too, against
the binding's own wgrender.

Web options are wgrender's web build settings, read from the environment:
  BACKEND=webgl2|webgpu   WEB_DEBUG=0|1   (e.g. BACKEND=webgpu ./build.py web)
Web builds are always WEB_THREADS=0: hxcpp's emscripten target is single-threaded.
Override the wgrender location with WGRENDER_DIR=/path/to/wgrender.
"""
import gzip
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
LIB = ROOT.parents[1]
if not (LIB / 'tools/wgrpath.py').exists():
    _found = subprocess.run(['haxelib', 'libpath', 'wgrender-hx'],
                            capture_output=True, text=True).stdout.strip()
    if not _found:
        sys.exit('wgrender-hx not found. Either keep this example inside the library, or:\n'
                 '  haxelib git wgrender-hx https://github.com/whirlinggizmo/wgrender-hx')
    LIB = pathlib.Path(_found)
sys.path.insert(0, str(LIB / 'tools'))
from wgrpath import find, host_os  # noqa: E402  # examples/simple-hxcpp -> the library
from guestbuild import check_library, desktop_variant as guest_desktop_variant  # noqa: E402
from hxcppweb import finish_site  # noqa: E402
WGRENDER = find(argv=[])
HAXE = os.environ.get('HAXE', 'haxe')
# The wgr binding, its generator and its host glue are the wgrender-hx haxelib.

def run(cmd, **kwargs):
    print('+', ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kwargs)


def desktop_variant():
    """<platform>/<variant> (the wg* layout), as wgr.macros.NativeOut names it: the
    toolchain hxcpp uses on Windows, MSVC unless HXCPP_MINGW."""
    return f'{host_os()}/{guest_desktop_variant()}'


def web_variant():
    """web/<variant>: wgrender's web settings, always without threads here."""
    return 'web/' + f'{web_backend()}-nothreads' + ('-debug' if web_debug() else '')


def web_backend():
    return os.environ.get('BACKEND') or 'webgl2'


def web_debug():
    return (os.environ.get('WEB_DEBUG') or '0') == '1'


def haxe(hxml, *extra):
    """The committed hxml, told which wgrender, from the example's directory."""
    check_library()
    run([HAXE, hxml, '-D', f'WGRENDER_DIR={WGRENDER}', *extra], cwd=ROOT)


def desktop_out():
    return ROOT / 'out' / desktop_variant()


def web_out():
    return ROOT / 'out' / web_variant()


def build_desktop():
    out = desktop_out()
    print(f'simple (desktop) -> {out.relative_to(ROOT)}/simple')
    haxe('build.hxml')
    out.mkdir(parents=True, exist_ok=True)
    exe = 'simple' + ('.exe' if host_os() == 'windows' else '')
    shutil.copy2(ROOT / 'build' / desktop_variant() / 'cpp' / exe, out / exe)
    # wgr.Assets looks for `assets` beside the executable, the same lookup a shipped
    # program uses. (A Windows build would copy or junction instead of linking.)
    link = out / 'assets'
    if link.is_symlink() or link.exists():
        link.unlink()
    link.symlink_to(WGRENDER / 'examples/assets')
    print(f'built {out}/simple (assets -> {link.readlink()})')


def build_web():
    site = web_out()
    work = ROOT / 'build' / web_variant()
    print(f'simple (web) -> {site.relative_to(ROOT)}/')
    haxe('web.hxml', *(['-D', 'wgr-webgpu'] if web_backend() == 'webgpu' else []),
         *(['--debug'] if web_debug() else []))
    site.mkdir(parents=True, exist_ok=True)
    for name in ('simple.js', 'simple.wasm'):
        shutil.copy2(work / 'cpp' / name, site / name)
    # the library's page (web/index.html), opening this example, with its source link
    finish_site(site, work, 'simple', 'examples/simple-hxcpp/src/Simple.hx')
    sizes()
    print(f'built {site.relative_to(ROOT)} — ./build.py serve, then open http://localhost:8000/')



def measure(directory):
    out = {}
    for name in ('simple.js', 'simple.wasm'):
        path = pathlib.Path(directory) / name
        if path.exists():
            out[name] = (path.stat().st_size, len(gzip.compress(path.read_bytes(), 9)))
    return out


def sizes():
    for name, (raw, packed) in measure(web_out()).items():
        print(f'{name}: {raw:,} bytes ({packed:,} gzipped)')


def compare():
    """Every port of this example that is built, side by side.

    Only comparable when each was built with the same wgrender web flags —
    BACKEND=webgl2 WEB_THREADS=0, which is what ./build.py web uses. The Nim port
    defaults to WEB_THREADS=1; build it with WEB_THREADS=0 to line the numbers up.
    """
    ports = [
        ('C (wgrender example)', WGRENDER / 'out/web/webgl2-nothreads'),
        ('Nim', LIB / '../wgrender-nim/examples/simple/out/web/webgl2-nothreads'),
        ('Beef', LIB / '../wgrender-beef/examples/simple/out/web/webgl2-nothreads'),
        ('Haxe (this)', web_out()),
    ]
    rows = [(label, measure(path)) for label, path in ports]
    baseline = next((m['simple.wasm'][0] for label, m in rows if label.startswith('C')), None)
    print(f'{"port":<22} {"wasm":>12} {"gzipped":>11} {"vs C":>8}   {"js":>9} {"gzipped":>9}')
    for label, m in rows:
        if 'simple.wasm' not in m:
            print(f'{label:<22} (not built)')
            continue
        wasm, wasm_gz = m['simple.wasm']
        js, js_gz = m.get('simple.js', (0, 0))
        over = f'{wasm / baseline:.2f}x' if baseline else '-'
        print(f'{label:<22} {wasm:>12,} {wasm_gz:>11,} {over:>8}   {js:>9,} {js_gz:>9,}')


def serve(port='8000'):
    run([sys.executable, LIB / 'tools/serve.py', port, web_out(), '--assets', WGRENDER / 'examples/assets', '--gzip'])


def clean():
    for d in ('out', 'build'):
        shutil.rmtree(ROOT / d, ignore_errors=True)
        print(f'removed {d}/')


def main():
    args = sys.argv[1:]
    command = args[0] if args else ''
    if command == 'desktop':
        build_desktop()
    elif command == 'web':
        build_web()
    elif command == 'all':
        build_desktop()
        build_web()
    elif command == 'serve':
        serve(*args[1:2])
    elif command == 'sizes':
        sizes()
    elif command == 'compare':
        compare()
    elif command == 'clean':
        clean()
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
