#!/usr/bin/env python3
"""Build and check the examples here.

    examples/build.py <command> [example ...]

Naming examples limits the command to those; naming none means all of them.

    all        build it: the web build (guest and host) and the native binary
    web        the guest and its wasm host -- one build, each example's build.web.hxml
    desktop    the native binary only, each example's build.desktop.hxml
    guest      (the same as web: the host is linked by the guest's build now)
    host       (likewise)
    drive      run each web build in a headless browser and fail on a console error
    site       collect every web build into out/web/<variant>/ with a page that lists them
    serve      build that site and serve it: one server, each example a subdirectory;
               name examples to serve just those (--tls cert key for https on 8443)
    sizes      what each web build weighs
    compare    those sizes against wgrender's own C build of the same example
    bench      frame cost, Haxe against C
    list       the examples, and what each one is
    clean      remove out/ (what the builds made) and build/ (their work)

So `examples/build.py desktop` builds every example's native binary, and
`examples/build.py all simple` builds one example every way.

Each example owns its build.py; this runs them, so there is one command to say
whether a change to the binding broke any of them. The library's own checks are
test/check.py at the root -- they are not an example's business and run on their own.

Python rather than a Makefile: nothing else in wgrender-hx uses make, and wgrender's
own tools that this shells out to are Python already, so it adds no dependency the
build did not have.

    WGRENDER_DIR   build against a wgrender of your own
    TLS_CERT/TLS_KEY   serve https without passing --tls
"""
import json
import os
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
LIB = HERE.parent
if not (LIB / 'tools/wgrpath.py').exists():
    # this tree has been copied out of the library; ask haxelib where it went
    _found = subprocess.run(['haxelib', 'libpath', 'wgrender-hx'],
                            capture_output=True, text=True).stdout.strip()
    if not _found:
        sys.exit('wgrender-hx not found. Either keep examples/ inside the library, or:\n'
                 '  haxelib git wgrender-hx https://github.com/whirlinggizmo/wgrender-hx')
    LIB = pathlib.Path(_found)
sys.path.insert(0, str(LIB / 'tools'))
from wgrpath import find  # noqa: E402
from guestbuild import Project, web_variant  # noqa: E402
WGRENDER = find(argv=[])
C_BUILD = WGRENDER / 'out/web/webgl2-nothreads'  # wgrender's own C build of each example


# simple-hxcpp is the other architecture -- Haxe through hxcpp into one wasm, rather
# than a JS guest on a wgrender host -- so it has its own commands and is not driven
# or benched with the guests.
GUESTS = ['hello', 'hello3d', 'particles', 'simple', 'stress', 'tick',
          'quit', 'window', 'font', 'audio', 'force_fetch',
          'fetch', 'sprite2d', 'sprite3d', 'text3d', 'tilemap',
          'model', 'meshes', 'textures', 'materials', 'lights',
          'instancing', 'shadows', 'render_target', 'postprocess', 'environment',
          'shaders', 'gamepad', 'scene3d', 'pick', 'loading', 'touch', 'ui']
OTHERS = ['simple-hxcpp']

WHAT = {
    'hello': 'a window, 2D shapes, text and the mouse',
    'hello3d': 'an orbiting camera over immediate-mode 3D; loads nothing, so it is '
               'the size floor',
    'particles': 'five emitters, 2D and 3D, with a click to burst confetti',
    'tick': 'a 10 Hz simulation against the render rate, raw and interpolated',
    'simple': 'a glTF model, a sprite, text, audio and picking',
    'quit': 'requesting a quit with audio playing and loads still in flight',
    'window': 'size, position, fullscreen and monitors, and what each platform refuses',
    'font': 'TrueType text at several sizes, measured and centred',
    'audio': 'looping mp3 and a one-shot ogg, and a stall that only the web hears',
    'force_fetch': 'a per-call source URL and a cache bypass, under a key that '
                   'resolves to nothing',
    'fetch': 'the desktop build downloading what the browser downloads, through a '
             'fetcher the app supplies',
    'sprite2d': 'screen-space sprites over a 3D scene: source rect, pivot, rotation, '
                'flip, picking',
    'sprite3d': 'the resource rule at its shortest: load, Texture.create, new Sprite3D',
    'text3d': 'text in the world, 3D shapes, and what a pick query costs',
    'tilemap': 'a scrolling 2D tile map with no Camera2D: sprite3d in the XY plane '
               'under an orthographic camera',
    'model': 'a glTF model created empty and filled in when its mesh arrives',
    'meshes': 'the seven generated shapes, sharing one normal-mapped material so '
              'their UVs and tangents have to agree',
    'textures': 'the same image as PNG and as .ktx, with what each costs in GPU '
                'memory and time',
    'materials': 'metallic-roughness rows, plus unlit, emissive, normal-mapped and '
                 'blended, all assigned before the mesh loads',
    'lights': 'directional, point and spot, each switchable, on a scene that starts '
              'with none',
    'instancing': '400 cubes sharing a mesh and a material, which is one draw; nothing '
                  'asks for it',
    'shadows': 'a casting light, and every knob that changes what it does',
    'render_target': 'drawing into textures: a low-res view, a minimap, and a label '
                     'a model wears',
    'postprocess': 'screen effects over the finished frame, each a custom shader in a '
                   'material',
    'environment': 'image-based lighting with no lights at all, plus background and '
                   'tone mapping',
    'shaders': 'four custom shaders: toon on a skinned model, dissolve, water that '
               'moves its own vertices, and sprite effects',
    'gamepad': 'every connected pad, live: sticks, triggers and buttons',
    'scene3d': 'retained shapes drawn by the scene, and clicking one to select it',
    'pick': 'ray-picking all three drawable kinds, with the alpha test letting a '
            'click through a sprite',
    'loading': 'six files loading while a cube spins, with a frame-time graph that '
               'shows what a background load costs against a synchronous one',
    'touch': 'every finger as a numbered ring, and two fingers panning, pinching and '
             'twisting a sprite',
    'ui': 'buttons, a progress bar and a clipped scrolling list built out of 2D '
          'shapes and text2d, beside a 3D model in the same scene',
    'simple-hxcpp': 'the same scene the other way: Haxe through hxcpp into one wasm, '
                    'for the size comparison',
}


def wanted(names, chosen=()):
    picked = [n for n in names if not chosen or n in chosen]
    if chosen and not picked and set(chosen) - set(GUESTS + OTHERS):
        sys.exit(f'no such example: {", ".join(sorted(set(chosen) - set(GUESTS + OTHERS)))}\n'
                 f'  have: {", ".join(GUESTS + OTHERS)}')
    return picked


def run(cmd, cwd, **kw):
    print(f'\n=== {cwd.name}: {" ".join(str(c) for c in cmd)}', flush=True)
    subprocess.run([str(c) for c in cmd], check=True, cwd=cwd, **kw)


def each(command, names=None, chosen=()):
    """A guest is its hxml files, so this does its work in-process; an all-in-one
    example (simple-hxcpp) is a different build and keeps its own script."""
    for name in wanted(names or GUESTS + OTHERS, chosen):
        if name in GUESTS:
            print(f'\n=== {name}: {command}', flush=True)
            Project(HERE / name, name).command(command)
        else:
            run([sys.executable, 'build.py', command], HERE / name)


# What an example's drive does beyond loading the page and checking it drew. Each
# example used to carry this in a drive stub of its own; particles was the
# only one that said anything, and without it the confetti path would go unexercised
# while the drive still passed.
DRIVE_FLAGS = {'particles': ['--click']}


def drive(chosen=()):
    env = dict(os.environ, WGRENDER_DIR=str(WGRENDER))  # drive.py drives the wgrender this builds with
    for name in wanted(GUESTS, chosen):
        run([sys.executable, LIB / 'tools/drive.py', f'--site={HERE / name / "out/web" / web_variant()}', f'--label={name}',
             *DRIVE_FLAGS.get(name, [])], HERE / name, env=env)
    if 'simple-hxcpp' in wanted(OTHERS, chosen):
        # all-in-one through hxcpp: the library's own page (web/index.html), so no guest host to wait for
        site = HERE / 'simple-hxcpp'
        run([sys.executable, LIB / 'tools/drive.py', f'--site={site / "out/web/webgl2-nothreads"}', '--label=haxe-simple',
             '--ready=examples.json', '--settle=8000', f'--shot={site / "build/web/webgl2-nothreads/check.png"}'], site, env=env)


def bench(chosen=()):
    if not C_BUILD.exists():
        sys.exit(f'no C builds at {C_BUILD}\n'
                 f'  cd {WGRENDER} && cmake --preset web-webgl2-nothreads && '
                 'cmake --build --preset web-webgl2-nothreads')
    for name in wanted(GUESTS, chosen):
        for args in ([f'--site={C_BUILD}', f'--url=/?ex={name}', '--probe=examples.json',
                      f'--label={name}-c'],
                     [f'--site={HERE / name / "out/web" / web_variant()}', f'--label={name}-haxe']):
            subprocess.run([sys.executable, str(WGRENDER / 'tools/bench/pagebench.py'), 'frame', *args], check=True)


SITE_INDEX = """<!doctype html>
<meta charset="utf-8">
<title>wgrender-hx examples</title>
<!-- Generated by examples/build.py site. -->
<style>
  :root {{ color-scheme: dark; }}
  body {{ margin: 0; padding: 2.5rem 1.5rem; background: #14141a; color: #e8e8ef;
         font: 15px/1.6 ui-monospace, SFMono-Regular, Menlo, monospace; }}
  main {{ max-width: 46rem; margin: 0 auto; }}
  h1 {{ font-size: 1.3rem; font-weight: 600; margin: 0 0 .3rem; }}
  p.lede {{ color: #9a9aad; margin: 0 0 2rem; }}
  select {{ width: 100%; margin: 0 0 2.5rem; padding: .6rem .7rem; background: #1c1c26;
           color: #e8e8ef; border: 1px solid #2a2a36; border-radius: 6px; font: inherit; }}
  select:hover {{ border-color: #3a3a4a; }}
  a {{ color: #66bfff; text-decoration: none; }}
  a:hover {{ text-decoration: underline; }}
  table {{ border-collapse: collapse; width: 100%; font-size: 13px; }}
  th, td {{ text-align: right; padding: .35rem .6rem; border-bottom: 1px solid #23232e; }}
  th:first-child, td:first-child {{ text-align: left; }}
  tr.name td {{ border-bottom: 0; padding-bottom: 0; }}
  td.what {{ text-align: left; color: #9a9aad; padding-top: 0; }}
  th {{ color: #9a9aad; font-weight: 500; }}
  caption {{ text-align: left; color: #9a9aad; padding-bottom: .6rem; }}
  noscript p {{ color: #ffb4b4; }}
</style>
<main>
<h1>wgrender-hx examples</h1>
<p class="lede">wgrender compiled to wasm as the host, the game compiled to JS as the
guest. The same sources build native through hxcpp.</p>

<select id="ex" aria-label="example">
  <option value="" selected disabled>run an example&hellip;</option>
{options}
</select>

<noscript><p>This picker needs JavaScript; the examples below are links.</p></noscript>

{table}
</main>
<script>
  const sel = document.getElementById("ex");
  sel.addEventListener("change", () => {{ location.href = "./" + sel.value + "/"; }});
</script>
"""


# On each example's page in the site: a bar above the canvas, as wgrender's own site
# has, with the way back, a picker that runs what it picks, and the example's source.
SITE_BAR_STYLE = """<style>
  #bar { position: fixed; inset: 0 0 auto 0; height: 38px; z-index: 10; display: flex;
    align-items: center; gap: 12px; padding: 0 12px; background: #1e2128;
    border-bottom: 1px solid #2c313c; color: #cdd3de; font: 14px/1.4 system-ui, sans-serif;
    white-space: nowrap; overflow: hidden; }
  /* a phone's bar has no room for the label: the picker says what it is */
  @media (max-width: 520px) { #exlabel { display: none; } }
  #bar a { color: #7ea2ff; text-decoration: none; }
  #bar a:hover { text-decoration: underline; }
  #bar b a { color: #7ea2ff; }
  #bar select { background: #0f1115; color: #cdd3de; border: 1px solid #3a4150;
    border-radius: 6px; padding: 4px 8px; font: inherit; }
  #canvas { inset: 38px 0 0 0; height: calc(100vh - 38px); }
  /* A console on the page, because a phone has no easy one: what the program logs,
     and anything the browser refuses to do. As wgrender's own page has it. */
  #log { position: fixed; inset: auto 0 0 0; max-height: 45vh; z-index: 11; display: none;
    overflow-y: auto; padding: 6px 10px; margin: 0; background: #0b0d11ee; color: #cdd3de;
    border-top: 1px solid #2c313c; font: 12px/1.45 ui-monospace, monospace;
    white-space: pre-wrap; overscroll-behavior: contain; }
  #log.on { display: block; }
  #log .warn { color: #ffcc66; }
  #log .err  { color: #ff8080; }
  #logbtn { margin-left: auto; background: #0f1115; color: #cdd3de; border: 1px solid #3a4150;
    border-radius: 6px; padding: 4px 8px; font: inherit; cursor: pointer; }
  #logbtn.alert { border-color: #ff8080; color: #ff8080; }
</style>
"""
# The console's script, from wgrender's page: everything the program prints goes to the
# panel as well as the browser's console, and the panel opens itself on the first error.
# boot.js sends all of wgrender's output to console.log, so a line's level comes from
# the line itself ("[WARN ] wgr_model.c:12: ..."). It runs before the page's module
# scripts, so it sees the first line.
SITE_CONSOLE = """<pre id="log"></pre>
<script>
  (() => {
    const logEl = document.getElementById("log");
    const logBtn = document.getElementById("logbtn");
    const MAX_LINES = 400;
    const LEVEL = /^\\[(TRACE|DEBUG|INFO|WARN|ERROR|FATAL)\\s*\\]/;
    function levelKind(text) {
      const m = LEVEL.exec(text);
      if (!m) return null;
      return (m[1] === "ERROR" || m[1] === "FATAL") ? "err" : m[1] === "WARN" ? "warn" : "";
    }
    function addLine(text, kind) {
      const line = document.createElement("span");
      line.className = kind || "";
      line.textContent = text + "\\n";
      logEl.appendChild(line);
      while (logEl.childNodes.length > MAX_LINES) logEl.removeChild(logEl.firstChild);
      logEl.scrollTop = logEl.scrollHeight;
      if (kind) logBtn.classList.add("alert");
      if (kind === "err" && !logEl.classList.contains("on")) logEl.classList.add("on");
    }
    logBtn.addEventListener("click", () => {
      logEl.classList.toggle("on");
      if (logEl.classList.contains("on")) logBtn.classList.remove("alert");
    });
    for (const [name, kind] of [["log", ""], ["info", ""], ["warn", "warn"], ["error", "err"]]) {
      const original = console[name].bind(console);
      console[name] = (...a) => {
        original(...a);
        const text = a.map(v => (typeof v === "string" ? v : String(v))).join(" ");
        addLine(text, kind || levelKind(text) || "");
      };
    }
    addEventListener("error", e => addLine("error: " + (e.message || e.type), "err"));
    addEventListener("unhandledrejection", e => addLine("unhandled: " + e.reason, "err"));
    // The page can start with it open: ?console=1, or #console
    if (new URLSearchParams(location.search).get("console") === "1" || location.hash === "#console") {
      logEl.classList.add("on");
    }
  })();
</script>
"""
SOURCE = 'https://github.com/whirlinggizmo/wgrender-hx/blob/main/examples/{name}/src/{main}.hx'


def main_class(name):
    """The example's main class, from its web hxml: the file its source link opens."""
    import re
    hxml = (HERE / name / 'build.web.hxml').read_text(encoding='utf-8')
    found = re.search(r'^--main\s+(\S+)', hxml, re.MULTILINE)
    if not found:
        sys.exit(f'{name}/build.web.hxml: no --main')
    return found.group(1).replace('.', '/')


def site_bar(name, names):
    options = ''.join(f'<option{" selected" if n == name else ""}>{n}</option>' for n in names)
    return (f'<div id="bar"><b><a href="../">wgrender-hx</a></b>'
            f'<label><span id="exlabel">example </span><select aria-label="example" '
            f'onchange="location.href = \'../\' + this.value + \'/\'">'
            f'{options}</select></label>'
            f'<a href="{SOURCE.format(name=name, main=main_class(name))}" target="_blank" '
            f'rel="noopener">source</a><button id="logbtn" type="button">console</button></div>\n'
            + SITE_CONSOLE)


def site(chosen=()):
    """Every built example under one directory, with a page that lists them: a
    self-contained site for any static host, at a domain root or under a path (the
    Pages workflow publishes it).

    It is a web build like any other, so it goes where one goes: out/web/<variant>/
    here, the variant being the examples' (js-webgl2-nothreads unless the web settings
    say otherwise), with each example in a directory of its own.

    Each example already generates a working page into its own out/web/<variant>/, so this
    copies those in side by side rather than building anything new. They cannot share
    a directory: every example has its own wgrender-host.wasm, exporting exactly the
    calls that guest makes, and they all have that name. The assets they load are
    wgrender's examples/assets, copied in once beside them (the benchmarks' models
    left out) with their manifests (tools/gen_manifest.py), and each page says so: <meta name="wgr-asset-base" content="../assets">,
    which wgr.Assets reads. Each page also gets the site's bar (SITE_BAR_STYLE, site_bar):
    the way back to the list, a picker, the example's source, and a console (SITE_CONSOLE).
    """
    import shutil
    out = HERE / 'out/web' / web_variant()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    options, built = [], []
    for name in wanted(GUESTS, chosen):
        src = HERE / name / 'out/web' / web_variant()
        if not (src / 'index.html').exists():
            print(f'{name}: not built, skipping (./build.py all)', file=sys.stderr)
            continue
        shutil.copytree(src, out / name)
        built.append(name)
        size = sum(f.stat().st_size for f in (out / name).rglob('*') if f.is_file())
        options.append(f'  <option value="{name}">{name} &mdash; {size:,} bytes</option>')
    if not built:
        sys.exit('nothing built: examples/build.py all')
    for name in built:
        page = out / name / 'index.html'
        html = page.read_text(encoding='utf-8')
        for old, new in (
                ('<meta charset="utf-8">', '<meta charset="utf-8">\n<meta name="wgr-asset-base" content="../assets">\n'
                 '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">'),
                ('</head>', SITE_BAR_STYLE + '</head>'),
                ('<body>', '<body>\n' + site_bar(name, built))):
            if old not in html:
                sys.exit(f'{page}: no {old} to put the site\'s additions at')
            html = html.replace(old, new, 1)
        page.write_text(html, encoding='utf-8')
    shutil.copytree(WGRENDER / 'examples/assets', out / 'assets', ignore=shutil.ignore_patterns('bench'))
    # the manifests the examples set (Assets.MANIFEST): a returning visitor then fetches
    # only the assets that changed since the last deploy
    subprocess.run([sys.executable, str(LIB / 'tools/gen_manifest.py'), str(out / 'assets')], check=True)
    (out / 'index.html').write_text(SITE_INDEX.format(
        options='\n'.join(options),
        table=example_table(built)), encoding='utf-8')
    print(f'site -> {out} ({len(built)} examples)')
    return out


def example_table(names):
    """Every example as a link to its page, with what it shows and, where wgrender's
    own C build of it is there, what each weighs against that."""
    import gzip
    import html

    def total(paths):
        data = b''.join(p.read_bytes() for p in paths)
        return sum(p.stat().st_size for p in paths), len(gzip.compress(data, 9))

    rows = []
    for name in names:
        site_dir = HERE / 'out/web' / web_variant() / name
        hx = [site_dir / 'wgrender-host.wasm', site_dir / 'wgrender-host.js', site_dir / f'{name}.js']
        c = [C_BUILD / f'{name}.wasm', C_BUILD / f'{name}.js']
        hraw, hgz = total(hx)
        if all(p.exists() for p in c):
            craw, cgz = total(c)
            cells = f'<td>{craw:,}</td><td>{cgz:,}</td><td>{hraw:,}</td><td>{hgz:,}</td><td>{hgz / cgz:.2f}x</td>'
        else:
            cells = f'<td>&ndash;</td><td>&ndash;</td><td>{hraw:,}</td><td>{hgz:,}</td><td>&ndash;</td>'
        rows.append(f'<tr class="name"><td><a href="./{name}/">{name}</a></td>{cells}</tr>'
                    f'<tr><td class="what" colspan="6">{html.escape(WHAT.get(name, ""))}</td></tr>')
    return ('<table><caption>Each example against wgrender\'s own C build of it, same '
            'backend and threading.</caption>'
            '<tr><th>example</th><th>C</th><th>C gzip</th><th>Haxe</th><th>Haxe gzip</th>'
            '<th>vs C</th></tr>' + ''.join(rows) + '</table>')


def serve(args, chosen=()):
    """Build the site and serve it with wgrender's own dev server.

    Plain HTTP on 8000 by default. --tls (or TLS_CERT/TLS_KEY in the environment,
    as wgrender's own serve-tls target takes them) serves HTTPS on 8443 instead.

    That matters for another device: localhost counts as a secure context whatever
    the scheme, but a phone on the LAN does not, and a threaded build needs a secure
    page for SharedArrayBuffer. The certificate has to name this machine's LAN
    address, which is why nothing here invents one.
    """
    cert = os.environ.get('TLS_CERT')
    key = os.environ.get('TLS_KEY')
    if '--tls' in args:
        i = args.index('--tls')
        if len(args) < i + 3:
            sys.exit('--tls takes a certificate and a key: ./build.py serve --tls cert.pem key.pem')
        cert, key = args[i + 1], args[i + 2]
        args = args[:i] + args[i + 3:]
    port = next((a for a in args if a.isdigit()), '8443' if cert else '8000')
    if (cert is None) != (key is None):
        sys.exit('TLS needs both a certificate and a key')

    out = site(chosen)
    cmd = [sys.executable, str(LIB / 'tools/serve.py'), port, str(out), '--assets', str(WGRENDER / 'examples/assets')]
    if cert:
        cmd += ['--tls', cert, key]
    scheme = 'https' if cert else 'http'
    print(f'\n{scheme}://localhost:{port}/')
    if not cert:
        print('  another device on the LAN needs https: ./build.py serve --tls cert.pem key.pem')
    subprocess.run(cmd, check=False)


def listing():
    for name in GUESTS + OTHERS:
        kind = 'guest' if name in GUESTS else 'all-in-one'
        print(f'  {name:<13} {kind:<11} {WHAT.get(name, "")}')


def main():
    args = sys.argv[1:]
    command = args[0] if args else ''
    rest = args[1:]
    # anything that is not a flag and names no file is an example to limit this to
    chosen = tuple(a for a in rest if not a.startswith('-') and not a.endswith('.pem')
                   and not a.isdigit())

    if command == 'list':
        listing()
    elif command == 'all':
        each('all', chosen=chosen)
    elif command in ('web', 'guest', 'host'):
        each('web', GUESTS, chosen)
        if command == 'web':
            each('web', OTHERS, chosen)
    elif command in ('desktop', 'sizes', 'clean'):
        each(command, chosen=chosen)
    elif command == 'drive':
        drive(chosen)
    elif command == 'site':
        site(chosen)
    elif command == 'serve':
        serve(rest, chosen)
    elif command == 'compare':
        # compare.py prints what is missing and why; a traceback on top of that adds
        # a stack trace to a message that was already the answer.
        return subprocess.run([sys.executable, str(LIB / 'tools/compare.py')]
                              + [str(HERE / n) for n in chosen]).returncode
    elif command == 'bench':
        bench(chosen)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    sys.exit(main() or 0)
