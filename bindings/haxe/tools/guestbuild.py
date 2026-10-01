#!/usr/bin/env python3
"""The suite's work for one example: its own hxml files, and the chores around them.

Each example is what a user would write: its src/, a build.web.hxml, and a
build.desktop.hxml. Those are the build -- `haxe build.web.hxml` from the example's
directory produces the same out/web this script does, host and all, because the wasm
host is linked by wgr.macros.WebHost from a line in that file. So this is not a build
system, and there is no script in each example to run it: examples/build.py imports it
and names the example. It is the things a copy of an example does not need and the
suite does:

- it refuses to build against the wrong copy of the binding (check_library), or a
  binding that is stale against wgrender's headers (check_binding);
- it passes -D WGRENDER_DIR, so the examples build against the wgrender this checkout
  is paired with -- a wgrender-c beside it, when there is one -- rather than only the
  pinned submodule. A user's build leaves it out and gets the submodule on both targets;
- after a desktop build it copies the binary out of hxcpp's scratch and puts an
  `assets` link beside it, where wgr.Assets looks. A user's own game ships its own
  assets and does not need that step;
- sizes and clean. Serving is examples/build.py's: one server, every example in a
  subdirectory of it.

`guest` and `host` still work and mean `web`: the guest and its host are one build now.

Env: WGRENDER_DIR, WEB_THREADS=0|1, BACKEND=webgl2|webgpu, WEB_DEBUG=0|1, HAXE.
"""
import gzip
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import find, host_os  # noqa: E402

LIB = pathlib.Path(__file__).resolve().parent.parent

# The examples' own builds follow the wg* layout (whirlinggizmo/.github CONVENTIONS.md,
# "Build directories"): what they make in out/<platform>/<variant>/, their work in
# build/. The web has two toolchains here, a JS guest (this) and hxcpp
# (tools/hxcppweb.py), so a guest's variant names it: js-webgl2-nothreads, the default
# every build.web.hxml names, or what BACKEND, WEB_THREADS and WEB_DEBUG choose.
DEFAULT_WEB = 'js-webgl2-nothreads'


def web_variant():
    """The guest web build's variant, from the settings WebHost builds the host with."""
    backend = os.environ.get('BACKEND') or 'webgl2'
    threads = (os.environ.get('WEB_THREADS') or '0') == '1'
    debug = (os.environ.get('WEB_DEBUG') or '0') == '1'
    return f'js-{backend}' + ('' if threads else '-nothreads') + ('-debug' if debug else '')


def desktop_variant():
    """hxcpp's native build: release, or on Windows the toolchain it used (MSVC by
    default, MinGW with HXCPP_MINGW or -D mingw)."""
    if host_os() != 'windows':
        return 'release'
    return 'mingw' if os.environ.get('HXCPP_MINGW') else 'msvc'


def check_library():
    """Is `-lib wgrender-hx` the copy this script came from?

    These are resolved separately and can disagree without saying so: this file is
    found by path, while the Haxe compile asks haxelib. A `haxelib git` install
    left over from testing an install, or a `dev` link that went away, and the
    build compiles against a different copy of the binding than the one being
    worked on -- which shows up much later as a type that does not exist, or worse,
    as an old one that does.
    """
    asked = subprocess.run(['haxelib', 'libpath', 'wgrender-hx'], capture_output=True, text=True)
    found = asked.stdout.strip()
    # haxelib reports a missing library on stdout with a zero exit, so the text is
    # what says whether it answered -- not the return code.
    if asked.returncode != 0 or not found or found.startswith('Error'):
        sys.exit(f'haxelib cannot resolve wgrender-hx:\n  {found or asked.stderr.strip()}\n'
                 f'  haxelib dev wgrender-hx {LIB}')
    if pathlib.Path(found).resolve() != LIB.resolve():
        sys.exit(f'-lib wgrender-hx resolves to {found}\n'
                 f'  but this build is running from {LIB}\n'
                 f'  haxelib dev wgrender-hx {LIB}')


class Project:
    def __init__(self, root, name):
        self.root = pathlib.Path(root).resolve()
        self.name = name
        self.variant = web_variant()
        self.site = self.root / 'out/web' / self.variant
        self.wgrender = find(argv=[])
        self.haxe = os.environ.get('HAXE', 'haxe')

    # ---------------------------------------------------------------- shell ---

    def run(self, cmd, **kw):
        print('+', ' '.join(str(c) for c in cmd), flush=True)
        subprocess.run([str(c) for c in cmd], check=True, **kw)

    def haxe_build(self, hxml):
        """The example's own hxml, told which wgrender this checkout builds against."""
        self.run([self.haxe, hxml, '-D', f'WGRENDER_DIR={self.wgrender}'], cwd=self.root)

    # --------------------------------------------------------------- checks ---

    def check_library(self):
        check_library()

    _checked = False

    def check_binding(self):
        """The binding is generated from wgrender's headers; a stale one declares an
        API that no longer exists, and a stale omissions list hides a decision."""
        if Project._checked:
            return  # the binding is the same for every example in one run of the suite
        Project._checked = True
        self.check_library()
        self.run([sys.executable, LIB / 'tools/gen_raw.py', '--check', self.wgrender])
        self.run([sys.executable, LIB / 'tools/coverage.py', '--check', self.wgrender])
        self.run([sys.executable, LIB / 'tools/refusals.py', '--check', self.wgrender])

    # ---------------------------------------------------------------- build ---

    def build_web(self):
        self.check_binding()
        print(f'web -> out/web/{self.variant} ({self.name}.js and the host it calls)')
        hxml = (self.root / 'build.web.hxml').read_text(encoding='utf-8')
        if self.variant != DEFAULT_WEB:
            # the committed hxml names the default variant; the web settings chose another
            hxml = hxml.replace(f'out/web/{DEFAULT_WEB}/', f'out/web/{self.variant}/')
            if f'out/web/{self.variant}/' not in hxml:
                sys.exit(f'{self.root}/build.web.hxml: no --js out/web/{DEFAULT_WEB}/ to redirect')
            variant_hxml = self.root / 'build/web' / self.variant / 'build.web.hxml'
            variant_hxml.parent.mkdir(parents=True, exist_ok=True)
            variant_hxml.write_text(hxml, encoding='utf-8')
            self.haxe_build(variant_hxml.relative_to(self.root))
        else:
            self.haxe_build('build.web.hxml')
        self.sizes()

    def build_desktop(self):
        self.check_binding()
        print(f'desktop -> out/{host_os()}/{desktop_variant()}/{self.name}-guest')
        self.haxe_build('build.desktop.hxml')
        self.install_desktop()

    def install_desktop(self):
        # out/<platform>/<variant>/, as wgrender's own builds are (the wg* layout). The
        # *command* stays `desktop`, which means native-not-web whichever OS it is.
        # hxcpp's work is in build/<platform>/<variant>/cpp, where wgr.macros.NativeOut
        # puts it: the committed hxml cannot know the host.
        variant = desktop_variant()
        cpp = self.root / 'build' / host_os() / variant / 'cpp'
        out = self.root / 'out' / host_os() / variant
        out.mkdir(parents=True, exist_ok=True)
        exe = f'{self.name}-guest' + ('.exe' if host_os() == 'windows' else '')
        shutil.copy2(cpp / exe, out / exe)
        # wgr.Assets looks for `assets` beside the executable, so a development build
        # gets one pointing at wgrender's tree -- the same lookup a shipped program
        # uses, rather than a path baked in at compile time.
        link = out / 'assets'
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(self.wgrender / 'examples/assets')
        print(f'built {out}/{self.name}-guest (assets -> {link.readlink()})')

    # ----------------------------------------------------------------- misc ---

    def sizes(self):
        rows = [('host wasm', self.site / 'wgrender-host.wasm'),
                ('host js', self.site / 'wgrender-host.js'),
                ('guest js', self.site / f'{self.name}.js')]
        total = 0
        for label, path in rows:
            if not path.exists():
                print(f'{label:<12} (not built)')
                continue
            raw = path.stat().st_size
            total += raw
            print(f'{label:<12} {raw:>10,} bytes ({len(gzip.compress(path.read_bytes(), 9)):>9,} gzipped)')
        if total:
            print(f'{"total":<12} {total:>10,} bytes')

    def clean(self):
        for d in ('out', 'build'):
            shutil.rmtree(self.root / d, ignore_errors=True)
        print('removed out/ and build/')

    def command(self, name):
        if name in ('web', 'guest', 'host'):
            self.build_web()
        elif name == 'desktop':
            self.build_desktop()
        elif name == 'all':
            self.build_web()
            self.build_desktop()
        elif name == 'sizes':
            self.sizes()
        elif name == 'clean':
            self.clean()
        else:
            sys.exit(f'{self.name}: no command {name!r}')
