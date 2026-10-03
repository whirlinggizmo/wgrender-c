#!/usr/bin/env python3
"""Build the JS binding's web site: wgrender as a wasm host, the binding, and the examples.

    bindings/js/tools/build_host.py [--webgpu] [--debug]

Writes out/wasm32/<variant>/site/js/ (wasm32-release's by default), beside the C
examples' site, so tools/serve_site.py serves it with /assets/ mounted:

    wgrender-host.js, .wasm   the host: wgrender's web library and the guest ABI glue
                              (bindings/host), linked with every call the binding makes
    wgrender.js, .d.ts, src/  the binding
    examples/<name>/          each example's page and script

The host exports every function in wgrender.exports.json (the generator's list), so any
program built on the binding runs against it: the full host, as a hot-reloading guest
needs. Trimming a host to what one program calls comes later, from a bundler's view of
what the program imports.

Runs tools/build_web_library.py first, which builds the library from build.json when
it's out of date. Needs emcc (exactly build.json's Emscripten).
"""
import json
import os
import pathlib
import shutil
import subprocess
import sys

WGRENDER = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(WGRENDER / 'tools'))
import builds  # noqa: E402
import cli  # noqa: E402
import headers  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, ('--webgpu', '--debug'), positional=0)

BINDING = WGRENDER / 'bindings/js'
HOST = WGRENDER / 'bindings/host'

# What the binding's JS reaches on the Emscripten module besides the exports.
RUNTIME_METHODS = ['addFunction', 'stringToUTF8', 'lengthBytesUTF8', 'UTF8ToString',
                   'stackAlloc', 'stackSave', 'stackRestore', 'HEAPU8', 'HEAP32', 'HEAPU32', 'HEAPF32']


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def main():
    backend = 'webgpu' if '--webgpu' in sys.argv else 'webgl2'
    debug = '--debug' in sys.argv
    preset = builds.web(backend=backend, debug=debug)
    _, variant = builds.split(preset)
    site = builds.programs(preset) / 'js'
    work = builds.work(preset) / 'js'
    work.mkdir(parents=True, exist_ok=True)

    run([sys.executable, WGRENDER / 'tools/build_web_library.py', f'BACKEND={backend}', 'WEB_THREADS=0',
         f'WEB_DEBUG={1 if debug else 0}'])
    target = json.loads((WGRENDER / 'build.json').read_text(encoding='utf-8'))['web'][variant]
    lib = builds.lib(preset) / 'libwgrender.a'

    listed = json.loads((BINDING / 'wgrender.exports.json').read_text(encoding='utf-8'))['functions']
    guest = sorted('_' + n for n in headers.functions_in(HOST / 'wgr_guest.h', WGRENDER, tool='build_host'))
    exports = ['_main'] + guest + sorted(set(listed)) + ['_malloc', '_free']
    (work / 'exports.txt').write_text('\n'.join(exports) + '\n', encoding='utf-8')

    site.mkdir(parents=True, exist_ok=True)
    emcc = shutil.which('emcc') or sys.exit('build_host: no emcc on PATH (source emsdk_env first)')
    run([emcc, '-O2', f'-I{WGRENDER / "include"}', f'-I{HOST}', *target['program_cflags'],
         HOST / 'wgr_guest.c', lib, *target['ldflags'],
         '-sALLOW_TABLE_GROWTH=1', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createWgrHost',
         f'-sEXPORTED_RUNTIME_METHODS={",".join(RUNTIME_METHODS)}',
         f'-sEXPORTED_FUNCTIONS=@{work / "exports.txt"}',
         '-o', site / 'wgrender-host.js'])

    for name in ('wgrender.js', 'wgrender.d.ts'):
        shutil.copy2(BINDING / name, site / name)
    shutil.copytree(BINDING / 'src', site / 'src', dirs_exist_ok=True)
    examples = sorted(p for p in (BINDING / 'examples').iterdir() if p.is_dir())
    for example in examples:
        shutil.copytree(example, site / 'examples' / example.name, dirs_exist_ok=True)
    print(f'build_host: {len(exports)} exports, {len(examples)} examples -> {os.path.relpath(site, WGRENDER)}')


if __name__ == '__main__':
    main()
