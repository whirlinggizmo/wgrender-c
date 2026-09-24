# Building wgrender

CMake (3.21 or newer) and Python 3 build everything, on Windows, Linux and macOS: the
library, the examples, the tests, the web builds and the tools. There is no make and
no shell script.

- [Desktop](#desktop): Windows (MSVC or MinGW), Linux, macOS
- [Web](#web-webgl2-and-webgpu): WebGL2 and WebGPU, with Emscripten
- [Windows from Linux](#windows-from-linux): MinGW, tested under Wine
- [Before calling a change done](#before-calling-a-change-done)
- [Generated files](#generated-files)
- [Benchmarks](#benchmarks)

The bindings build wgrender from the same description (`build.json`) with their own
toolchains; each has a BUILDING.md of its own.

## Desktop

CMake (3.21 or newer) and Python 3, on Windows, Linux or macOS; Ninja, or Visual Studio
on Windows (open this folder: it reads the presets). Every build is a preset in
`CMakePresets.json` and builds into `build/<preset>/`:

```sh
cmake --preset desktop && cmake --build --preset desktop     # library + every example
build/desktop/simple            # from this directory: examples load examples/assets from here
cmake --preset headless && cmake --build --preset headless   # no window, GPU or audio device
ctest --preset headless         # unit tests, guardrails, and every example headless for ~3 s
python3 tools/verify.py         # desktop, headless, ThreadSanitizer (and Windows, below):
                                # run before calling a change done
```

`headless` builds use sokol's dummy GPU backend and no window or audio: they run frames
at 60/s until `wgr_request_quit()`, or for `WGR_HEADLESS_FRAMES` frames when that
environment variable is set. `debug`, `tsan`, `asan` and `ubsan` are the other desktop
presets.

On Windows, Visual Studio needs nothing more. From a command line, run the presets in a
"x64 Native Tools Command Prompt" (or after `vcvars64.bat`), so MSVC is on `PATH`; with
Ninja, CMake takes a `gcc` it finds on `PATH` (Nim's MinGW, say) over MSVC's `cl`, so set
`CC=cl` to be sure. The sanitizer presets are gcc/clang's, so `verify.py` runs `desktop`
and `headless` there.

On Linux, sokol links the system's audio, GL and X11 libraries, so their dev packages
must be installed; configuring checks and names any that are missing:

```sh
python3 tools/deps.py install   # via apt / dnf / pacman (uses sudo)
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
same file to compile wgrender with their own toolchains (`tools/buildweb.py` builds the
web library from it with nothing but emsdk). Checked on Windows 11 with Visual Studio
2026 (MSVC 19.51): the library and all examples, no warnings, and the examples run.

## Web: WebGL2 and WebGPU

Needs Emscripten: `$EMSDK` set, or `emcc` on `PATH` (`source <emsdk>/emsdk_env.sh`).

```sh
cmake --preset web-webgl2 && cmake --build --preset web-webgl2   # every example -> build/web-webgl2/
python3 tools/serve.py 8000 build/web-webgl2   # http://localhost:8000/ (assets mounted at /assets/)
node tools/webcheck.mjs                        # load each in a browser, fail on errors
node tools/webcheck.mjs --backend=webgpu       # the same for WebGPU (web-webgpu)
node tools/webstart.mjs                        # startup times per example: cold, warm and hot visits
python3 tools/verify.py --web                  # all of the above web builds, checked
tools/benchmarks.py --all                      # C and every sibling binding -> docs/benchmarks.md
```

The web presets are `web-webgl2`, `web-webgpu`, their `-nothreads` builds, and
`web-webgl2-debug`.

`tools/webcheck.mjs` needs Node >= 22 and a Chromium-based
browser (Brave, Chrome or Chromium; override with `WEBCHECK_BROWSER`). It checks
four examples at a time, each in its own browser context, waits until each has
finished loading its assets, and fails an example on console errors, wgrender
`[ERROR]`/`[FATAL]` logs, exceptions, sokol panics, a wrong/missing backend, or
assets still loading after 20 s. It saves a screenshot of each to
`build/web-<backend>/webcheck/`. WebGL2
runs headless; WebGPU needs a GPU adapter, which headless browsers lack, so it runs on
a virtual X display when Xvfb is installed, else in a visible window. It catches
crashes, errors and unfinished loads, not wrong-looking output, so glance at the
screenshots. The browser and
server it starts are always stopped, even if Node crashes or is killed (process
groups, a sweep by the run's unique profile directory, and a watchdog).

### Startup and hosting

A built site (`build/web-<backend>/`) loads each program as `name.js?v=<hash>`
and `name.wasm?v=<hash>`: `tools/webdeploy.py` writes every file's hash into
`index.html`, so a file's URL changes when its content does. The page starts
downloading the wasm alongside the JS, and it compiles as it streams. To start fast,
a host should send:

- `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` (threaded builds; the `-nothreads`
  builds don't need them)
- `Content-Type: application/wasm` for `.wasm` (else it can't compile while streaming)
- `Cache-Control: public, max-age=31536000, immutable` for versioned requests
  (`?v=`), and `no-cache` for the page: a returning visit then revalidates only the
  page and fetches no code (a CDN must keep the query string in its cache key)
- gzip or brotli for `.js`, `.wasm` and `.html`

`tools/site.py` copies a build and the assets it loads into `<build>/site/`, ready for
any static host. `tools/serve.py` sends no-store by default (every reload gets the
latest build); `--cache --gzip` serves as above. `tools/webstart.mjs` opens each
example three times in a fresh browser profile
(cold, warm, and hot: Chrome's compiled-code cache), locally and on emulated 4G, and
times the download, compile, wgrender's init, the first frame and the end of asset
loading from `wgr:*` performance marks. `--devtools` and `--url` measure another
device's browser, such as a phone through `adb forward`.

## Windows from Linux

With MinGW-w64 (`sudo apt install mingw-w64`), Windows builds come from Linux:

```sh
cmake --preset windows && cmake --build --preset windows    # build/windows/*.exe (OpenGL)
cmake --preset windows-headless && cmake --build --preset windows-headless
ctest --preset windows-headless   # unit tests and every example headless, under Wine
```

The tests go through `tools/wine.py`: `$WINE`, else `wine64` / `wine`
on `PATH`, else the newest Proton in a Steam library (Library > Tools). Its prefix is
`build/wine`. The `.exe` files are linked statically (no MinGW DLLs to ship).
`tools/verify.py` builds `windows` when MinGW is installed, and tests
`windows-headless` when there's a Wine, so Windows code keeps compiling. Wine runs the
windowed examples too (OpenGL through the host's driver), but their windows, audio and
gamepads on real Windows are only checked by hand.

## Before calling a change done

```sh
python3 tools/verify.py         # desktop, headless (unit tests, guardrails, smoke), tsan,
                                # and windows / windows-headless when MinGW and Wine are there
python3 tools/verify.py --web   # also every example on web-webgl2, -nothreads and web-webgpu,
                                # loaded in the browser: for rendering, assets or web code
```

A web example has to link for EM_JS bodies to be checked (closure runs then), so after
touching one, build one: `cmake --build --preset web-webgl2 --target hello`.

## Generated files

Committed, and rebuilt by hand when what they come from changes:

| File | From | Rebuild |
| --- | --- | --- |
| `src/shaders/*.glsl.h` | `src/shaders/*.glsl` | `python3 tools/gen_shaders.py` (target `gen-shaders`) |
| `examples/assets/shaders/*.wgrshader` | `examples/shaders/*.glsl`, `shaders/wgr.glsl` | `python3 tools/gen_shaders.py --examples` (target `gen-example-shaders`) |
| `src/data/wgr_brdf_lut.h` | `wgri_environment_brdf_lut` | target `gen-brdf-lut` of a desktop preset |

sokol-shdc is fetched into `build/tools` the first time, at the version
`deps/sokol/VERSION` pins. The vendored sokol and Clay are updated with
`tools/update_sokol.py` and `tools/update_clay.py`.

## Benchmarks

```sh
python3 tools/bench/run.py spritebench [--desktop]   # also shadowbench, loadbench [--ktx]
cmake --build --preset web-webgl2 --target benches   # the web pages: /bench/?ex=spritebench
python3 tools/benchmarks.py --all                    # C and every binding -> docs/benchmarks.md
```
