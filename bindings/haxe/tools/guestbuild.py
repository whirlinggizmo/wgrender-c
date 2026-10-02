#!/usr/bin/env python3
"""The suite's work for one example: its own hxml files, and the chores around them.

Each example is what a user would write: its src/, a build.web.hxml, and a
build.desktop.hxml. Those are the build -- `haxe build.web.hxml` from the example's
directory produces the same site this script does, host and all, because the wasm
host is linked by wgr.macros.WebHost from a line in that file. So this is not a build
system, and there is no script in each example to run it: tools/run_examples.py imports it
and names the example. It is the things a copy of an example does not need and the
suite does:

- it refuses to build against the wrong copy of the binding (check_library), or a
  binding that is stale against wgrender's headers (check_binding);
- after a desktop build it copies the binary out of hxcpp's scratch and puts an
  `assets` link beside it, where wgr.Assets looks. A user's own game ships its own
  assets and does not need that step;
- sizes and clean. Serving is tools/run_examples.py's: one server, every example in a
  subdirectory of it.

`guest` and `host` still work and mean `web`: the guest and its host are one build now.

Env: WEB_THREADS=0|1, BACKEND=webgl2|webgpu, WEB_DEBUG=0|1, HAXE.
"""
import gzip
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from wgrpath import WGRENDER, exe, native_preset, web_variant  # noqa: E402
import builds  # noqa: E402  (wgrender's)

LIB = pathlib.Path(__file__).resolve().parent.parent

# The examples' own builds are named as wgrender's (tools/wgrpath.py): what they make
# in out/<platform>/<variant>/, their work in build/<preset>/. A guest's web site is
# out/wasm32/<variant>/site: release, the default every build.web.hxml names, or what
# BACKEND, WEB_THREADS and WEB_DEBUG choose. hxcpp's web build (tools/build_hxcpp_example.py) adds
# -hxcpp to the variant.
DEFAULT_WEB = 'release'


def main_class(example, hxml):
    """The main class an example's hxml names, as the Haxe compiler reads its command
    line (wgr.macros.Members.mainClass): 'Hello', or 'pkg.Main'."""
    # first on the command line, so it runs before the hxml's own init macros (WebHost's)
    done = subprocess.run([os.environ.get('HAXE', 'haxe'), '--macro', 'wgr.macros.Members.mainClass()',
                           hxml, '--no-output'],
                          cwd=example, capture_output=True, text=True)
    found = done.stdout.strip().splitlines()[-1] if done.stdout.strip() else ''
    if done.returncode != 0 or not found:
        sys.exit(f'{example}/{hxml}: no main class\n{done.stderr.strip()}')
    return found


def site_dir(variant):
    """Where an example's web build goes, relative to the example."""
    return f'out/wasm32/{variant}/site'


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
        self.site = self.root / site_dir(self.variant)
        self.wgrender = WGRENDER
        self.haxe = os.environ.get('HAXE', 'haxe')

    # ---------------------------------------------------------------- shell ---

    def run(self, cmd, **kw):
        print('+', ' '.join(str(c) for c in cmd), flush=True)
        subprocess.run([str(c) for c in cmd], check=True, **kw)

    def haxe_build(self, hxml):
        """The example's own hxml, told which wgrender this checkout builds against."""
        self.run([self.haxe, hxml], cwd=self.root)

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
        self.run([sys.executable, LIB / 'tools/gen_raw_externs.py', '--check'])
        self.run([sys.executable, LIB / 'tools/check_coverage.py', '--check'])
        self.run([sys.executable, LIB / 'tools/check_refusals.py', '--check'])

    # ---------------------------------------------------------------- build ---

    def build_web(self):
        self.check_binding()
        print(f'web -> {site_dir(self.variant)} ({self.name}.js and the host it calls)')
        hxml = (self.root / 'build.web.hxml').read_text(encoding='utf-8')
        if self.variant != DEFAULT_WEB:
            # the committed hxml names the default variant; the web settings chose another
            hxml = hxml.replace(f'{site_dir(DEFAULT_WEB)}/', f'{site_dir(self.variant)}/')
            if f'{site_dir(self.variant)}/' not in hxml:
                sys.exit(f'{self.root}/build.web.hxml: no --js {site_dir(DEFAULT_WEB)}/ to redirect')
            variant_hxml = self.root / 'build' / f'wasm32-{self.variant}' / 'build.web.hxml'
            variant_hxml.parent.mkdir(parents=True, exist_ok=True)
            variant_hxml.write_text(hxml, encoding='utf-8')
            self.haxe_build(variant_hxml.relative_to(self.root))
        else:
            self.haxe_build('build.web.hxml')
        self.sizes()

    def build_desktop(self):
        self.check_binding()
        print(f'desktop -> {builds.split(native_preset())[0]}: out/.../bin/{self.name}-guest')
        self.haxe_build('build.desktop.hxml')
        self.install_desktop()

    def install_desktop(self):
        # out/<platform>/<variant>/bin, as wgrender's own builds are. The *command* stays
        # `desktop`, which means native-not-web whichever OS it is. hxcpp's work is in
        # build/<preset>/cpp, where wgr.macros.NativeOut puts it: the committed hxml
        # cannot know the host.
        preset = native_preset()
        platform_name, variant = builds.split(preset)
        cpp = self.root / 'build' / preset / 'cpp'
        out = self.root / 'out' / platform_name / variant / 'bin'
        out.mkdir(parents=True, exist_ok=True)
        program = exe(f'{self.name}-guest')
        shutil.copy2(cpp / program, out / program)
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
        if name == 'web':
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


def finish_site(site, work, name, source):
    """The page for an all-in-one build in SITE: web/index.html opening NAME, its source
    link to SOURCE (a path in this repository), finished by tools/finish_site.py with
    the versioned file names and examples.json. The page is written in WORK first."""
    page = (LIB / 'web/index.html').read_text(encoding='utf-8')
    for mark in ('/*wgr:first*/"simple-hxcpp"', '/*wgr:source*/"'):
        if mark not in page:
            sys.exit(f'{LIB}/web/index.html: no {mark} to fill in')
    page = page.replace('/*wgr:first*/"simple-hxcpp"', f'/*wgr:first*/"{name}"')
    page = __import__('re').sub(r'/\*wgr:source\*/"[^"]*"',
                  f'/*wgr:source*/"https://github.com/whirlinggizmo/wgrender-c/blob/main/bindings/haxe/{source}"', page)
    shell = work / 'index.html'
    shell.write_text(page, encoding='utf-8')
    subprocess.run([sys.executable, str(WGRENDER / 'tools/finish_site.py'), str(site), str(shell)], check=True)
    shell.unlink()


class HxcppProject:
    """An example built all-in-one through hxcpp (Haxe -> C++ -> native or wasm), rather
    than as a JS guest on a wasm host: its build.hxml (native) and web.hxml (the web,
    hxcpp's emscripten target, always without threads), as `program`. The web build
    gets the library's own page (web/index.html)."""

    def __init__(self, root, name, program, source):
        self.root = pathlib.Path(root).resolve()
        self.name, self.program, self.source = name, program, source
        self.haxe = os.environ.get('HAXE', 'haxe')

    def run(self, cmd):
        print('+', ' '.join(str(c) for c in cmd), flush=True)
        subprocess.run([str(c) for c in cmd], check=True, cwd=self.root)

    def desktop_out(self):
        """out/<platform>/<variant>/bin, the preset as wgr.macros.NativeOut names it."""
        platform_name, variant = builds.split(native_preset())
        return self.root / 'out' / platform_name / variant / 'bin'

    def web_out(self):
        """out/wasm32/<variant>/site: wgrender's web settings, never threads, plus -hxcpp."""
        return self.root / site_dir(web_variant(hxcpp=True))

    def build_desktop(self):
        out = self.desktop_out()
        print(f'{self.name} (desktop) -> {out.relative_to(self.root)}/{self.program}')
        check_library()
        self.run([self.haxe, 'build.hxml'])
        out.mkdir(parents=True, exist_ok=True)
        program = exe(self.program)
        shutil.copy2(self.root / 'build' / native_preset() / 'cpp' / program, out / program)
        # wgr.Assets looks for `assets` beside the executable, the same lookup a shipped
        # program uses
        link = out / 'assets'
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(WGRENDER / 'examples/assets')

    def build_web(self):
        site, variant = self.web_out(), web_variant(hxcpp=True)
        work = self.root / 'build' / f'wasm32-{variant}'
        print(f'{self.name} (web) -> {site.relative_to(self.root)}/')
        check_library()
        self.run([self.haxe, 'web.hxml', *(['-D', 'wgr-webgpu'] if '-webgpu' in variant else []),
                  *(['--debug'] if variant.startswith('debug') else [])])
        site.mkdir(parents=True, exist_ok=True)
        for leaf in (f'{self.program}.js', f'{self.program}.wasm'):
            shutil.copy2(work / 'cpp' / leaf, site / leaf)
        finish_site(site, work, self.program, self.source)
        self.sizes()

    def measure(self, directory):
        out = {}
        for leaf in (f'{self.program}.js', f'{self.program}.wasm'):
            path = pathlib.Path(directory) / leaf
            if path.exists():
                out[leaf] = (path.stat().st_size, len(gzip.compress(path.read_bytes(), 9)))
        return out

    def sizes(self):
        for leaf, (raw, packed) in self.measure(self.web_out()).items():
            print(f'{leaf}: {raw:,} bytes ({packed:,} gzipped)')

    def compare(self):
        """Its wasm beside wgrender's C build of the same example, both with the same web
        flags (BACKEND=webgl2, no threads: wasm32-release)."""
        wasm = f'{self.program}.wasm'
        rows = [('C (wgrender example)', self.measure(WGRENDER / 'out/wasm32/release/site')),
                (f'Haxe ({self.name})', self.measure(self.web_out()))]
        baseline = rows[0][1].get(wasm, (None,))[0]
        print(f'{"port":<22} {"wasm":>12} {"gzipped":>11} {"vs C":>8}')
        for label, m in rows:
            if wasm not in m:
                print(f'{label:<22} (not built)')
                continue
            raw, packed = m[wasm]
            print(f'{label:<22} {raw:>12,} {packed:>11,} {f"{raw / baseline:.2f}x" if baseline else "-":>8}')

    def clean(self):
        for d in ('out', 'build'):
            shutil.rmtree(self.root / d, ignore_errors=True)

    def command(self, name):
        if name == 'web':
            self.build_web()
        elif name == 'desktop':
            self.build_desktop()
        elif name == 'all':
            self.build_desktop()
            self.build_web()
        elif name == 'sizes':
            self.sizes()
        elif name == 'compare':
            self.compare()
        elif name == 'clean':
            self.clean()
        else:
            sys.exit(f'{self.name}: no command {name!r}')
