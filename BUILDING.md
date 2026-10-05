# Building wgrender

CMake (3.21 or newer) and Python 3 build everything, on Windows, Linux and macOS: the
library, the examples, the tests, the web builds and the tools. There is no make and
no shell script. [Requirements](#requirements) lists what to install.

## Requirements

Every tool is Python, standard library only: nothing to `pip install`, and no Node.
What the tools set up themselves (below) needs nothing from you. Everything under
Optional is only for what its row names: without it the rest builds and tests, and
`tools/verify_builds.py` says `SKIPPING` for what it couldn't check (the Haxe binding
without Haxe, the Windows build without MinGW or Wine).

### Linux

Required:

- [CMake](https://cmake.org/download/) 3.21 or newer, and [Ninja](https://ninja-build.org/)
- [Python](https://www.python.org/downloads/) 3.9 or newer
- gcc or clang to build, and clang for the tests: the rule check (`tools/check_rules.py`,
  the `check` test that `ctest` and `verify_builds.py` run) reads the code through it.
  Emscripten's counts, and is tried first, so with emsdk installed there's nothing more
  to get; else the system's
- the GL, X11 and ALSA dev packages: `python3 tools/setup_system_packages.py install`
  (apt, dnf or pacman); a desktop configure checks for them

Optional:

| For | Needs |
| --- | --- |
| Web builds (`wasm32-*`) | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk), exactly the version `build.json` pins (`"emscripten"`; see [Web](#web-webgl2-and-webgpu)) |
| Web examples checked in a browser (`verify_builds.py --web`, `check_web_examples.py`, `measure_example_startup.py`) | a Chromium-based browser ([Chrome](https://www.google.com/chrome/), [Chromium](https://www.chromium.org/getting-involved/download-chromium/), [Brave](https://brave.com/download/) or [Edge](https://www.microsoft.com/edge/download)) |
| WebGPU checked without a window | Xvfb (`xvfb`); without it a visible window opens |
| Windows builds (`windows-x64-mingw-*`), cross-built | MinGW-w64 (`mingw-w64`), and [Wine](https://gitlab.winehq.org/wine/wine/-/wikis/Download) or Steam's Proton to run their tests |
| A Windows machine checked over ssh (`verify_on_windows.py`) | ssh to it, with the Windows requirements there |
| Only if you work on the Haxe binding (`bindings/haxe`) | [Haxe](https://haxe.org/download/) 4.3.7 and [hxcpp](https://lib.haxe.org/p/hxcpp/) 4.3.2 (`haxelib install hxcpp 4.3.2`), with a C++ compiler; [its BUILDING.md](bindings/haxe/BUILDING.md) |

### macOS (Apple silicon)

Required:

- [CMake](https://cmake.org/download/) 3.21 or newer, and [Ninja](https://ninja-build.org/)
  (`brew install cmake ninja`)
- [Python](https://www.python.org/downloads/) 3.9 or newer
- Apple's clang: `xcode-select --install`; it also serves the tests' rule check (as
  Emscripten's does, tried first, when emsdk is installed)

Optional:

| For | Needs |
| --- | --- |
| Web builds (`wasm32-*`) | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk), exactly the version `build.json` pins (`"emscripten"`; see [Web](#web-webgl2-and-webgpu)) |
| Web examples checked in a browser | a Chromium-based browser ([Chrome](https://www.google.com/chrome/), [Chromium](https://www.chromium.org/getting-involved/download-chromium/), [Brave](https://brave.com/download/) or [Edge](https://www.microsoft.com/edge/download)) |
| Windows builds (`windows-x64-mingw-*`), cross-built | MinGW-w64 (`brew install mingw-w64`), and [Wine](https://gitlab.winehq.org/wine/wine/-/wikis/Download) to run their tests |
| Only if you work on the Haxe binding (`bindings/haxe`) | [Haxe](https://haxe.org/download/) 4.3.7 and [hxcpp](https://lib.haxe.org/p/hxcpp/) 4.3.2 (`haxelib install hxcpp 4.3.2`), with a C++ compiler; [its BUILDING.md](bindings/haxe/BUILDING.md) |

### Windows

Required, by the compiler you build with:

- [CMake](https://cmake.org/download/) 3.21 or newer, and
  [Python](https://www.python.org/downloads/) 3.9 or newer, either way
- MSVC (`windows-x64-msvc-*`): [Visual Studio](https://visualstudio.microsoft.com/downloads/)
  with the C++ workload. Its presets use Visual Studio's generator, so no developer
  prompt is needed
- MinGW (`windows-x64-mingw-*`): [Ninja](https://ninja-build.org/); the compiler is set up
  for you (below)
- clang for the tests: the rule check (`tools/check_rules.py`, the `check` test) reads
  the code through it. Emscripten's counts, and is tried first, so with emsdk installed
  there's nothing more to get; else [LLVM's](https://releases.llvm.org/)

Optional:

| For | Needs |
| --- | --- |
| Web builds (`wasm32-*`) | [Emscripten](https://emscripten.org/docs/getting_started/downloads.html) (emsdk), exactly the version `build.json` pins (`"emscripten"`; see [Web](#web-webgl2-and-webgpu)) |
| Web examples checked in a browser | a Chromium-based browser ([Chrome](https://www.google.com/chrome/), [Chromium](https://www.chromium.org/getting-involved/download-chromium/), [Brave](https://brave.com/download/) or [Edge](https://www.microsoft.com/edge/download)) |
| Only if you work on the Haxe binding (`bindings/haxe`) | [Haxe](https://haxe.org/download/) 4.3.7 and [hxcpp](https://lib.haxe.org/p/hxcpp/) 4.3.2 (`haxelib install hxcpp 4.3.2`), with a C++ compiler; [its BUILDING.md](bindings/haxe/BUILDING.md); hxcpp builds with MSVC unless told otherwise |

### Set up by the tools

The first time they're needed, into the per-user cache (`~/.cache/wgrender` on Linux,
`~/Library/Caches/wgrender` on macOS, `%LOCALAPPDATA%\wgrender` on Windows;
`WGR_CACHE_DIR` moves it): sokol-shdc, the shader compiler
(pinned, SHA-256 checked, from upstream or the robknopf mirror); on Windows the pinned
MinGW-w64 ([WinLibs](https://winlibs.com/), `tools/setup_mingw.py`, run by the MinGW
presets' configure); a Basis Universal encoder for compressed textures
(`tools/compress_textures.py`, built with your C++ compiler); the Wine prefix. Nothing
else is downloaded, and nothing at all once these are set up.

- [Desktop](#desktop): Windows (MSVC or MinGW), Linux, macOS
- [Web](#web-webgl2-and-webgpu): WebGL2 and WebGPU, with Emscripten
- [Windows from Linux](#windows-from-linux): MinGW, tested under Wine
- [Before calling a change done](#before-calling-a-change-done)
- [Tools](#tools)
- [Generated files](#generated-files)
- [Benchmarks](#benchmarks)

The bindings under `bindings/` build wgrender from the same description (`build.json`)
with their own toolchains; each has a BUILDING.md of its own
([bindings/haxe](bindings/haxe/BUILDING.md)).

## Desktop

CMake (3.21 or newer) and Python 3, on Windows, Linux or macOS; Ninja, or Visual Studio
on Windows (open this folder: it reads the presets). Every build is a preset in
`CMakePresets.json`, named `<platform>-<variant>`. The platform is what a program links
against: `linux-x64`, `macos-arm64`, `windows-x64-msvc`, `windows-x64-mingw` or
`wasm32`. The variant is `release` or `debug`, then whatever the build adds, in order:
a backend other than the default (`webgpu`), options (`headless`, `threads`), a
sanitizer. A name only adds: a build without threads is plain `wasm32-release`, never
`-nothreads`.

What a preset makes goes to `out/<platform>/<variant>/`: the library in `lib/`
(`libwgrender.a`; MSVC's `wgrender.lib`), the programs in `bin/`, and for the web the
site in `site/`. The build writes all of it, so deleting `out/` is a clean. CMake's own
work (its cache, the objects) stays in `build/<preset>/`, so `out/` is only results. On
Linux:

```sh
cmake --preset linux-x64-release && cmake --build --preset linux-x64-release   # library + every example
out/linux-x64/release/bin/simple        # from this directory: examples load examples/assets from here
cmake --preset linux-x64-debug-headless && cmake --build --preset linux-x64-debug-headless   # no window, GPU or audio device
ctest --preset linux-x64-debug-headless   # unit tests, guardrails, and every example headless for ~3 s
python3 tools/verify_builds.py         # release, headless, ThreadSanitizer (and Windows, below):
                                # run before calling a change done
```

On a Mac the same presets start `macos-arm64-`. Each machine lists only its own presets
and the ones it can cross-build (`cmake --list-presets`):

| Platform | Presets |
| --- | --- |
| Linux, macOS | `-release`, `-debug`, `-debug-headless`, and `-debug-tsan`, `-debug-asan`, `-debug-ubsan` (the unit tests under a sanitizer) |
| Windows, MSVC | `windows-x64-msvc-release`, `windows-x64-msvc-debug`, `windows-x64-msvc-debug-headless` |
| Windows, MinGW | `windows-x64-mingw-release`, `windows-x64-mingw-debug-headless` |
| Web | `wasm32-release`, `wasm32-release-webgpu`, each also `-threads`; `wasm32-debug`, `wasm32-debug-threads` |

The tests run on debug builds: assertions on, and sanitizer reports with their stack
traces intact.

`headless` builds use sokol's dummy GPU backend and no window or audio: they run frames
at 60/s until `wgr_request_quit()`, or for `WGR_HEADLESS_FRAMES` frames when that
environment variable is set.

Every preset treats warnings as errors (`-Werror`, `/WX` for MSVC), so a new one stops
the build where it appears rather than scrolling past. Some only show in the debug and
sanitizer builds, where the compiler traces more (`tools/verify_builds.py` runs the headless
and ThreadSanitizer builds, as CI does). A project that builds wgrender as part of its
own (`add_subdirectory`) doesn't get this, so a newer compiler's new warning can't break
it; `-DWGR_WERROR=ON` or `OFF` decides either way.

On Windows, the `windows-x64-msvc` presets use Visual Studio's generator, from Visual
Studio itself or any command line, with no developer prompt; they build
with the static C runtime (`/MT`, `/MTd` for debug), as every wg* library does, so the
`.lib` links into Beef and other static-runtime programs as it is. The
`windows-x64-mingw` presets work from any shell: they build with a pinned MinGW-w64 (a
WinLibs GCC), which `tools/setup_mingw.py` downloads, checks against its SHA-256 and
unpacks into the per-user cache the first time they configure, never with whichever
`gcc` is on `PATH`. There are no sanitizer presets on Windows, so `verify_builds.py` runs
`windows-x64-msvc-release` and `windows-x64-msvc-debug-headless` there.

On Linux, sokol links the system's audio, GL and X11 libraries, so their dev packages
must be installed; configuring checks and names any that are missing:

```sh
python3 tools/setup_system_packages.py install   # via apt / dnf / pacman (uses sudo)
# or manually, e.g. Debian/Ubuntu:
#   sudo apt install libasound2-dev libgl-dev libx11-dev libxi-dev libxcursor-dev
```

A program of your own takes the library, its public headers and the platform libraries
it links (OpenGL, X11, ALSA, the Windows libraries, the macOS frameworks) with:

```cmake
add_subdirectory(wgrender-c)
target_link_libraries(my_game PRIVATE wgrender)
```

What's built, and with what, is `build.json`: the sources, and per target the defines,
flags and libraries. The CMake build lists none of its own, and the bindings read the
same file to compile wgrender with their own toolchains (`tools/build_web_library.py` builds the
web library from it with nothing but emsdk). Checked on Windows 11 with Visual Studio
2026 (MSVC 19.51): the library and all examples, no warnings, and the examples run.

## Web: WebGL2 and WebGPU

Needs Emscripten, exactly the version `build.json` pins (`"emscripten"`, which CI installs
too): `emsdk install <version> && emsdk activate <version>`, then `$EMSDK` set, or `emcc`
on `PATH` (`source <emsdk>/emsdk_env.sh`). Configuring a web preset, and
`tools/build_web_library.py`, refuse another version, naming both; set
`WGRENDER_EMSCRIPTEN_VERSION=<version>` to build with another on purpose (announced every
time), and change the pin in `build.json` to move to it.

```sh
cmake --preset wasm32-release && cmake --build --preset wasm32-release   # every example -> out/wasm32/release/site/
python3 tools/serve_site.py 8000 out/wasm32/release/site   # http://localhost:8000/ (assets mounted at /assets/)
python3 tools/check_web_examples.py                     # load each in a browser, fail on errors
python3 tools/check_web_examples.py --backend=webgpu    # the same for WebGPU (wasm32-release-webgpu)
python3 tools/measure_example_startup.py                     # startup times per example: cold, warm and hot visits
python3 tools/verify_builds.py --web                  # all of the above web builds, checked
tools/run_benchmarks.py --all                      # C and every binding -> docs/benchmarks.md
```

The web presets are `wasm32-release` and `wasm32-release-webgpu`, each also with
`-threads`, and `wasm32-debug` and `wasm32-debug-threads`. Without threads is the
default: it runs on any static host. `tools/build_web_library.py` builds only the library, for
any of the eight combinations of backend, threads and debug, into the same
`out/wasm32/<variant>/lib/` as the preset of that name (its objects in
`build/wasm32-<variant>/buildweb/`).

`tools/check_web_examples.py` needs a Chromium-based browser: Brave, Chrome, Chromium or Edge,
found on PATH or where they install (override with `WEBCHECK_BROWSER`), and nothing
but Python's standard library: it drives the browser over the DevTools protocol
itself (`tools/browser.py`). It checks
four examples at a time, each in its own browser context, waits until each has
finished loading its assets, and fails an example on console errors, wgrender
`[ERROR]`/`[FATAL]` logs, exceptions, sokol panics, a wrong/missing backend, or
assets still loading after 20 s. It saves a screenshot of each to
`build/<preset>/webcheck/`, beside the build rather than in the site. WebGL2
runs headless; WebGPU needs a GPU adapter, which headless browsers lack, so it runs on
a virtual X display when Xvfb is installed (Linux), else in a visible window. It catches
crashes, errors and unfinished loads, not wrong-looking output, so glance at the
screenshots. The browser and
server it starts are always stopped, even if the check crashes or is killed (process
groups, or process trees on Windows, a sweep by the run's unique profile directory, and
a watchdog, `tools/watch_browser.py`).

### Startup and hosting

A built site (`out/wasm32/<variant>/site/`) loads each program as `name.js?v=<hash>`
and `name.wasm?v=<hash>`: `tools/finish_site.py` writes every file's hash into
`index.html`, so a file's URL changes when its content does. The page starts
downloading the wasm alongside the JS, and it compiles as it streams. To start fast,
a host should send:

- `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` (only the `-threads` builds need
  them)
- `Content-Type: application/wasm` for `.wasm` (else it can't compile while streaming)
- `Cache-Control: public, max-age=31536000, immutable` for versioned requests
  (`?v=`), and `no-cache` for the page: a returning visit then revalidates only the
  page and fetches no code (a CDN must keep the query string in its cache key)
- gzip or brotli for `.js`, `.wasm` and `.html`
- `ETag` or `Last-Modified` on assets, and an answer to `If-None-Match` /
  `If-Modified-Since`: a returning visit checks a cached asset that isn't fresh by its
  `Cache-Control` with a conditional request, and a 304 costs no download. Any
  `max-age` works (GitHub Pages sends 600); how long it is, is how long a changed
  asset may go unnoticed without a manifest

Assets are cached by libwgrender (IndexedDB), not by the browser's HTTP cache, which
it goes past when it asks. A manifest (`tools/gen_manifest.py DIR`, then
`wgr_asset_set_manifest("manifest.json")`) removes those checks: a returning visit
asks only about the root manifest, and fetches only the assets whose hash changed.
It matters most on a host that stamps every file at deploy, as GitHub Pages does:
there each deploy changes every asset's `ETag` and `Last-Modified`, so without a
manifest a returning visit downloads them all again. Regenerate the manifests on every
deploy, after the last file is in place.

`tools/build_site.py` copies a build and the assets it loads into `<build>/site/`, with
their manifests, ready for any static host. `tools/serve_site.py` sends no-store by default (every reload gets the
latest build); `--cache --gzip` serves as above. `tools/measure_example_startup.py` opens each
example three times in a fresh browser profile
(cold, warm, and hot: Chrome's compiled-code cache), locally and on emulated 4G, and
times the download, compile, wgrender's init, the first frame and the end of asset
loading from `wgr:*` performance marks. `--devtools` and `--url` measure another
device's browser, such as a phone through `adb forward`.

## Windows from Linux

With MinGW-w64 (`sudo apt install mingw-w64`), Windows builds come from Linux:

```sh
cmake --preset windows-x64-mingw-release && cmake --build --preset windows-x64-mingw-release   # out/windows-x64-mingw/release/bin/*.exe (OpenGL)
cmake --preset windows-x64-mingw-debug-headless && cmake --build --preset windows-x64-mingw-debug-headless
ctest --preset windows-x64-mingw-debug-headless   # unit tests and every example headless, under Wine
```

The tests go through `tools/run_windows_program.py`: `$WINE`, else `wine64` / `wine`
on `PATH`, else the newest Proton in a Steam library (Library > Tools). Its prefix (a
fake Windows install, shared by every build) is in the per-user cache,
`~/.cache/wgrender/wine` (`tools/hostcache.py`; `WGR_CACHE_DIR` or `WINEPREFIX` move it). The `.exe` files are linked statically (no MinGW DLLs to ship).
`tools/verify_builds.py` builds `windows-x64-mingw-release` when MinGW is installed, and tests
`windows-x64-mingw-debug-headless` when there's a Wine, so Windows code keeps compiling. Wine runs the
windowed examples too (OpenGL through the host's driver), but their windows, audio and
gamepads on real Windows are only checked by hand. To build and test on a real Windows
machine without pushing, `tools/verify_on_windows.py HOST [--msvc]` copies the working
tree there over ssh, runs the presets, and deletes it all after.

## Before calling a change done

What a change has to pass, and which extra checks which change needs, is a rule:
[docs/CONVENTIONS.md](docs/CONVENTIONS.md), "Build and verify". The commands:

```sh
python3 tools/verify_builds.py                  # release, headless (unit tests, guardrails, smoke), tsan,
                                         # windows-x64-mingw-* with MinGW and Wine, the Haxe suite with Haxe
python3 tools/verify_builds.py --web            # also every example on the web presets, in a browser,
                                         # and the Haxe examples built for the web and driven
python3 tools/verify_builds.py --windows HOST   # also MinGW and MSVC on a Windows machine over ssh
cmake --build --preset wasm32-release --target hello   # links a web example: what checks EM_JS
```

## Tools

Every tool takes `--help`. Beside the ones above:

- `tools/check_asset_cache.py [--manifest] [--backend=webgpu] [--threads]` -- the web
  asset cache across visits: tilemap in one browser context while its sheet is kept,
  changed and deleted on the server, and the network blocked; each visit judged by its
  requests' statuses and the screen. `--manifest` does it with manifests.
- `tools/measure_example_sizes.py [out/wasm32/<variant>/site]` -- wasm and JS sizes per
  web example (raw and gzip; brotli if installed).
- `tools/serve_site.py [port] [site]` -- the dev server (COOP/COEP headers, `/assets/`
  mounted) on http://localhost:8000. `--tls CERT KEY` serves HTTPS for other devices on
  the LAN (a phone), which need a secure page for threaded builds; `--assets DIR` mounts
  DIR at `/assets/` instead.
- `tools/compress_textures.py [--linear] name.png...` -- compressed texture files beside
  each PNG (`name.bc7.ktx`, `.astc.ktx`, `.etc2.ktx`), loaded as `name.ktx`; builds a
  pinned Basis Universal encoder into the per-user cache the first time. `--gltf
  model.gltf` does a model's textures and writes `model.ktx.gltf`.
- `tools/pack_shader.py name.glsl` -- compile a custom material shader (written against
  `shaders/wgr.glsl`) into `name.wgrshader` for every backend.
- `tools/bench/run_benchmark.py loadbench [--desktop] [--ktx]` -- the worst frame while
  loading large glTF models on create, with an upload budget and without (it downloads
  them on first use); `--ktx` with their textures compressed (needs `--desktop`:
  headless samples no compressed format).
- `tools/bench/run_benchmark.py shadowbench [--desktop]` -- what a casting light costs a
  frame: no shadows, one light at three map sizes, two lights, one where nothing
  receives, one where every model shares a mesh and material (what instancing is worth),
  and two facing away from everything (culling on and off), at four model counts.
  Headless is CPU only; `--desktop` opens a window with vsync off for real frame times.
- `tools/bench/run_benchmark.py spritebench [--desktop]` -- sprite-heavy scenes: frame
  time, the CPU split into update, scene and submit, and sokol_gl's vertex and command
  use.
- The benchmarks are targets of the web presets too (`loadbench`, `shadowbench`,
  `spritebench`, `stress`; `benches` for all): pages of their own under `bench/` in the
  site, `/bench/?ex=spritebench` (results in the browser console).
- `tools/run_benchmarks.py [--doc | --all]` -- the C `simple` against every binding
  (`docs/benchmarks.md`): download size, frame cost, JS heap and GC, and what a call from
  a JS guest costs. It measures the C into `bench/results.json` and collects each
  binding's own; `--doc` only regenerates the page, `--all` also runs each binding's
  `tools/run_benchmarks.py`. The harness is `tools/bench/` (`measure.py`, which bindings
  import; `measure_page.py`; `callbench/`; and `stress.c`, the scene the bindings port:
  `/bench/?ex=stress&n=5000`). The stress runs need Xvfb and a GPU. By hand, not CI;
  commit both files.

## Generated files

Committed (the manifests excepted), and rebuilt when what they come from changes:

| File | From | Rebuild |
| --- | --- | --- |
| `src/shaders/*.glsl.h` | `src/shaders/*.glsl` | `python3 tools/gen_shaders.py` (target `gen-shaders`) |
| `examples/assets/shaders/*.wgrshader` | `examples/shaders/*.glsl`, `shaders/wgr.glsl` | `python3 tools/gen_shaders.py --examples` (target `gen-example-shaders`) |
| `src/data/wgr_brdf_lut.h` | `wgri_environment_brdf_lut` | target `gen-brdf-lut` of a Linux, macOS or Windows preset |
| `bindings/haxe/src/wgr/impl/Raw.*.hx` | `include/*.h` | `python3 bindings/haxe/tools/gen_raw_externs.py` |
| `bindings/js/wgrender.js`, `.d.ts`, `.exports.json` | `include/*.h` | `python3 bindings/js/tools/gen_binding.py` |
| `manifest.json` in each directory of a site's assets (not committed) | the files beside it | `python3 tools/gen_manifest.py DIR` (`tools/build_site.py` runs it); every deploy, after the last file is in place |

sokol-shdc is fetched into the per-user cache (`tools/hostcache.py`) the first time, at the version
`deps/sokol/VERSION` pins. The vendored sokol and Clay are updated with
`tools/update_sokol.py` and `tools/update_clay.py`.

## Benchmarks

```sh
python3 tools/bench/run_benchmark.py spritebench [--desktop]   # also shadowbench, loadbench [--ktx]
cmake --build --preset wasm32-release --target benches   # the web pages: /bench/?ex=spritebench
python3 tools/run_benchmarks.py --all                    # C and every binding -> docs/benchmarks.md
```
