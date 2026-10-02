# Building wgrender-hx

A native build needs no build tool and no library built first: hxcpp compiles
wgrender's C sources with the same toolchain it compiles your program with
(`project/Build.xml`, from wgrender's `build.json`). A web build is a JS guest on a
wasm host, and the `wgr.macros.WebHost` macro builds and links the host for you. So a
build is `haxe <file>.hxml`, on Windows, Linux or macOS.

## What you need

- Haxe 4.3 and hxcpp (`haxelib install hxcpp`). Tested with Haxe 4.3.7, and with hxcpp
  4.3.2, the one haxelib serves (CI pins it), and hxcpp's current master. A newer hxcpp
  should work; `haxelib.json` doesn't pin one, so a user isn't held back
- a C and C++ compiler for native builds:
  - Linux and macOS: gcc or clang
  - Windows: MSVC (Visual Studio; hxcpp's default), or MinGW. Always add
    `-D HXCPP_M64`: hxcpp builds 32-bit on Windows unless told otherwise.
- for the web: Emscripten (emsdk), with `emcc` on `PATH`. wgrender's web library is
  built by its `tools/build_web_library.py`, on the Python emsdk brings.
- on Linux, the system's GL, X11 and ALSA dev packages, which sokol links:
  `python3 tools/setup_system_packages.py install` from the repository root (apt, dnf or pacman)
- Python 3 for the tools here (`tools/run_examples.py`, `tools/check_binding.py`, the generators)
- a Chromium-based browser (Brave, Chrome, Chromium or Edge) for `tools/run_examples.py drive`
  (`tools/drive_example.py`, Python like the rest: there is no Node to install)

## Install

```sh
haxelib git wgrender-hx https://github.com/whirlinggizmo/wgrender-c main bindings/haxe
```

That clones wgrender-c and makes `bindings/haxe` the library's root; wgrender is two
directories up. Working on the binding itself, from a checkout:
`haxelib dev wgrender-hx bindings/haxe`.

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

The `--macro` line is optional: it moves the C++ to `build/<preset>/cpp`
(`build/linux-x64-release/cpp`, `build/windows-x64-msvc-release/cpp`, ...), which an
hxml shared between OSes can't name itself.

For the web, from one like `examples/hello/build.web.hxml`: the guest compiled to JS,
and one line that makes its host (`wgrender-host.js` and `.wasm`), `boot.js` and an
`index.html` beside it:

```
-cp src
-lib wgrender-hx
--main Hello
--js out/wasm32/release/site/hello.js
--macro wgr.macros.WebHost.build()
```

The host exports exactly the calls the guest makes; the README's "A web guest, from
your own hxml" has the options (`-D wgr-host=full`, `-D wgr-build-dir`, ...). The web
build is chosen by the environment, spelled as wgrender's own tools spell it:
`BACKEND=webgl2|webgpu`, `WEB_THREADS=0|1` (0 by default here: a threaded page needs
COOP/COEP headers), `WEB_DEBUG=0|1`.

## The examples

```sh
tools/run_examples.py all            build each one, web and native
tools/run_examples.py web simple     only the web build, only simple
tools/run_examples.py serve          serve every web build (http://localhost:8000/)
tools/run_examples.py drive          run each web build in a headless browser
tools/run_examples.py site           every web build and the assets, one site for any static host
tools/run_examples.py compare        sizes against wgrender's own C build of each
```

`tools/run_examples.py` builds against the wgrender this binding sits in and puts
wgrender's sample assets beside each desktop binary.

Builds are named as wgrender's are (its `tools/builds.py`): a preset
`<platform>-<variant>`, what it makes in `out/<platform>/<variant>/`, its work in
`build/<preset>/` (hxcpp's C++ and objects in `build/linux-x64-release/cpp/`,
WebHost's host cache in `build/wasm32-release/webhost/`, the drive's screenshot
beside it). An example's guest web build is `out/wasm32/release/site/` (the web
settings pick another variant, such as `release-threads` with `WEB_THREADS=1`), its
desktop binary `out/linux-x64/release/bin/` (`out/windows-x64-msvc/release/bin/`,
...), and `site` gathers every guest into `examples/out/wasm32/release/site/`.
`compare` needs wgrender's C web examples built: `cmake --preset wasm32-release &&
cmake --build --preset wasm32-release` in wgrender-c.

`examples/simple-hxcpp` is `simple` built all-in-one through hxcpp, for the web too:
`tools/run_examples.py desktop simple-hxcpp` or `web simple-hxcpp` (`out/linux-x64/release/bin/`,
`out/wasm32/release-hxcpp/site/`: hxcpp's web build adds `-hxcpp` to the variant).
Like every build of the binding it compiles wgrender in from its sources
(`project/Build.xml`, with the flags `project/wgrender.xml` carries from wgrender's
`build.json`), by whichever compiler hxcpp uses: MSVC or MinGW on Windows, emcc for the
web. On Windows, `wgr.macros.NativeOut` tells hxcpp's emscripten target where emcc and
emsdk's Python are. `tools/build_hxcpp_example.py <example>` builds any example that way for the
web (`out/wasm32/release-hxcpp/site/`), for the benchmarks.

## Checks

```sh
python3 tools/check_binding.py            # generators in --check mode, then the binding against
                                 # headless wgrender (-D wgr-headless, compiled in),
                                 # native and js
python3 tools/check_refusals.py --check --require-clang   # (CI) every refusal documented
```

`tools/check_binding.py` builds in `build/<os>/headless/` and needs only Haxe, hxcpp and a C
compiler. After wgrender changes, regenerate what is generated from it: `tools/gen_raw_externs.py` (the C surface),
`tools/gen_keys.py`, `tools/gen_hxcpp_sources.py` (`project/wgrender.xml`); `tools/check_binding.py`
says which is stale.

## Benchmarks

`python3 tools/run_benchmarks.py` builds `simple` and `stress` for the web (as a guest, and
through hxcpp) and measures them with wgrender's harness against its C baseline (run
wgrender's `tools/run_benchmarks.py` first), into `bench/results.json` and
[docs/benchmarks.md](docs/benchmarks.md). By hand, not in CI; the stress scene needs
Xvfb and a GPU.
