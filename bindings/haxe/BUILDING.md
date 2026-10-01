# Building wgrender-hx

A native build needs no build tool and no library built first: hxcpp compiles
wgrender's C sources with the same toolchain it compiles your program with
(`project/Build.xml`, from wgrender's `build.json`). A web build is a JS guest on a
wasm host, and the `wgr.macros.WebHost` macro builds and links the host for you. So a
build is `haxe <file>.hxml`, on Windows, Linux or macOS.

## What you need

- Haxe 4.3 and hxcpp (`haxelib install hxcpp`)
- a C and C++ compiler for native builds:
  - Linux and macOS: gcc or clang
  - Windows: MSVC (Visual Studio; hxcpp's default), or MinGW. Always add
    `-D HXCPP_M64`: hxcpp builds 32-bit on Windows unless told otherwise.
- for the web: Emscripten (emsdk), with `emcc` on `PATH`. wgrender's web library is
  built by its `tools/buildweb.py`, on the Python emsdk brings.
- on Linux, the system's GL, X11 and ALSA dev packages, which sokol links:
  `python3 project/lib/wgrender-c/tools/deps.py install` (apt, dnf or pacman)
- Python 3 for the tools here (`examples/build.py`, `test/check.py`, the generators)
- a Chromium-based browser (Brave, Chrome, Chromium or Edge) for `examples/build.py drive`
  (`tools/drive.py`, Python like the rest: there is no Node to install)

## Install

```sh
haxelib git wgrender-hx https://github.com/whirlinggizmo/wgrender-hx
```

`haxelib git` clones the wgrender submodule too, under `project/lib/wgrender-c`. After
every `haxelib update wgrender-hx`, run `haxelib run wgrender-hx setup`: it moves the
submodule to the commit this library pins (the binding is generated from wgrender's
headers, and refuses to build against others). `setup web` also builds wgrender's web
library, which the web build does anyway when it needs it. `haxelib run wgrender-hx
where` says what is present.

Working on the binding itself: `haxelib dev wgrender-hx /path/to/wgrender-hx`.

## Build a program

Native, from an hxml like `examples/hello/build.desktop.hxml`:

```
-cp src
-lib wgrender-hx
--main Hello
--cpp build/cpp
--macro wgr.macros.NativeOut.build()
-D HXCPP_M64
```

The `--macro` line is optional: it moves the C++ to `build/<os>/<variant>/cpp`
(`build/linux/release/cpp`, `build/windows/msvc/cpp`, ...), which an hxml shared
between OSes can't name itself.

For the web, from one like `examples/hello/build.web.hxml`: the guest compiled to JS,
and one line that makes its host (`wgrender-host.js` and `.wasm`), `boot.js` and an
`index.html` beside it:

```
-cp src
-lib wgrender-hx
--main Hello
--js out/web/js-webgl2-nothreads/hello.js
--macro wgr.macros.WebHost.build()
```

The host exports exactly the calls the guest makes; the README's "A web guest, from
your own hxml" has the options (`-D wgr-host=full`, `-D wgr-build-dir`, ...). The web
build is chosen by the environment, spelled as wgrender's own tools spell it:
`BACKEND=webgl2|webgpu`, `WEB_THREADS=0|1` (0 by default here: a threaded page needs
COOP/COEP headers), `WEB_DEBUG=0|1`.

`-D WGRENDER_DIR=<path>` builds against a wgrender of your own instead of the pinned
submodule, for the native and web builds alike.

## The examples

```sh
examples/build.py all            build each one, web and native
examples/build.py web simple     only the web build, only simple
examples/build.py serve          serve every web build (http://localhost:8000/)
examples/build.py drive          run each web build in a headless browser
examples/build.py site           every web build and the assets, one site for any static host
examples/build.py compare        sizes against wgrender's own C build of each
```

`examples/build.py` builds against this checkout's wgrender (a `../wgrender-c` beside
this repository, else the submodule; `WGRENDER_DIR` overrides) and puts wgrender's
sample assets beside each desktop binary.

Builds follow the wg* layout (whirlinggizmo/.github CONVENTIONS.md, "Build
directories"): what they make in `out/<platform>/<variant>/`, their work in
`build/<platform>/<variant>/` (hxcpp's C++ and objects in `build/linux/release/cpp/`,
WebHost's host cache in `build/web/js-webgl2-nothreads/webhost/`, the drive's
screenshot beside it). An example's guest web build is `out/web/js-webgl2-nothreads/`
(`js-` because the web has two toolchains here; the web settings pick another variant,
such as `js-webgl2` with `WEB_THREADS=1`), its desktop binary `out/linux/release/`
(`out/windows/msvc/`, ...), and `site` gathers every guest into
`examples/out/web/js-webgl2-nothreads/`. `compare` needs wgrender's C web examples
built: `cmake --preset web-webgl2-nothreads && cmake --build --preset
web-webgl2-nothreads` in wgrender-c.

`examples/simple-hxcpp` is `simple` built all-in-one through hxcpp, for the web too:
`./build.py desktop` or `./build.py web` (`out/linux/release/`, `out/web/webgl2-nothreads/`).
Like every build of the binding it compiles wgrender in from its sources
(`project/Build.xml`, with the flags `project/wgrender.xml` carries from wgrender's
`build.json`), by whichever compiler hxcpp uses: MSVC or MinGW on Windows, emcc for the
web. On Windows, `wgr.macros.NativeOut` tells hxcpp's emscripten target where emcc and
emsdk's Python are. `tools/hxcppweb.py <example>` builds any example that way for the
web (`out/web/hxcpp-webgl2-nothreads/`), for the benchmarks.

## Checks

```sh
python3 test/check.py            # generators in --check mode, then the binding against
                                 # headless wgrender (-D wgr-headless, compiled in),
                                 # native and js
python3 tools/refusals.py --check --require-clang   # (CI) every refusal documented
```

`test/check.py` builds in `build/<os>/headless/` and needs only Haxe, hxcpp and a C
compiler. After wgrender changes, regenerate what is generated from it: `tools/gen_raw.py` (the C surface),
`tools/gen_keys.py`, `tools/gen_sources.py` (`project/wgrender.xml`); `test/check.py`
says which is stale.

## Benchmarks

`python3 tools/benchmarks.py` builds `simple` and `stress` for the web (as a guest, and
through hxcpp) and measures them with wgrender's harness against its C baseline (run
wgrender's `tools/benchmarks.py` first), into `bench/results.json` and
[docs/benchmarks.md](docs/benchmarks.md). By hand, not in CI; the stress scene needs
Xvfb and a GPU.
