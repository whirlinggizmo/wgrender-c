#!/usr/bin/env python3
"""Check the JS binding end to end, and fail on anything wrong:

    bindings/js/tools/check_binding.py

1. wgrender.js, wgrender.d.ts and wgrender.exports.json are current with the headers
   (gen_binding.py --check);
2. the declarations hold up under TypeScript: tests/types.ts compiles with --strict, and
   each mistake it marks @ts-expect-error is caught. Needs tsc: `tsc` on PATH, or the
   TSC environment variable naming one, run with the node on PATH or emsdk's
   (EMSDK_NODE). Without tsc this step is SKIPPED, loudly;
3. the host and site build (build_host.py, wasm32-release);
4. every example runs in a headless browser with no console error and draws something
   (the Haxe binding's tools/drive_example.py, one page per example).

Needs emcc (build.json's Emscripten), clang, and a Chromium-based browser.
"""
import os
import pathlib
import shutil
import subprocess
import sys

WGRENDER = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(WGRENDER / 'tools'))
import builds  # noqa: E402
import cli  # noqa: E402

if __name__ == '__main__':
    cli.parse(__doc__, (), positional=0)

BINDING = WGRENDER / 'bindings/js'
TOOLS = BINDING / 'tools'


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], **kw).returncode


def main():
    failed, skipped = [], []
    if run([sys.executable, TOOLS / 'gen_binding.py', '--check']) != 0:
        failed.append('the binding is stale: run bindings/js/tools/gen_binding.py')

    tsc = os.environ.get('TSC') or shutil.which('tsc')
    if tsc:
        # tsc is a node script; run it through a node found here rather than its
        # `#!/usr/bin/env node`: with emsdk's environment, PATH may hold no node at all
        # (and its root holds a directory named node), but EMSDK_NODE names emsdk's own.
        node = shutil.which('node') or os.environ.get('EMSDK_NODE')
        launch = [node, tsc] if node and not tsc.endswith('.exe') else [tsc]
        if run([*launch, '--noEmit', '--strict', '--target', 'es2022', '--module', 'es2022',
                '--moduleResolution', 'bundler', 'tests/types.ts'], cwd=BINDING) != 0:
            failed.append('tests/types.ts: TypeScript disagrees with the declarations')
    else:
        skipped.append('the TypeScript check (no tsc on PATH, and TSC not set)')
        print('SKIPPING the TypeScript check (no tsc on PATH, and TSC not set)', flush=True)

    if run([sys.executable, TOOLS / 'build_host.py']) != 0:
        sys.exit('check_binding: FAIL: the host did not build')

    site = builds.programs(builds.web()) / 'js'
    shots = builds.work(builds.web()) / 'js'
    for example in sorted(p.name for p in (BINDING / 'examples').iterdir() if p.is_dir()):
        if run([sys.executable, WGRENDER / 'bindings/haxe/tools/drive_example.py', f'--site={site}',
                f'--page=/examples/{example}/', f'--label=js-{example}', f'--shot={shots / (example + ".png")}']) != 0:
            failed.append(f'examples/{example}')

    if failed:
        sys.exit('check_binding: FAIL: ' + '; '.join(failed))
    print('check_binding: PASS' + (f' (SKIPPED: {"; ".join(skipped)})' if skipped else ''))


if __name__ == '__main__':
    main()
