# AGENTS

Conventions for working in **libwgrender** (a sokol-based C library, evolved from librl).
Keep this file short and rule-shaped. The authoritative design doc is
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build & verify

The build is CMake (3.21+; Ninja, or Visual Studio on Windows) and Python 3. There is no
make and no shell script: everything below works the same on Windows, Linux and macOS.

- **A build is a preset `<platform>-<variant>`.** The platform is what a program links
  against: `linux-x64`, `macos-arm64`, `windows-x64-msvc`, `windows-x64-mingw`,
  `wasm32`. The variant is the configuration, `release` or `debug`, always named, then
  what the build *adds*, in order: a backend other than the platform's default
  (`webgpu`), options (`headless`, `threads`), a sanitizer (`tsan`, `asan`, `ubsan`).
  A name only ever adds: `-threads`, never `-nothreads`. So `linux-x64-debug-headless`,
  `wasm32-release`, `wasm32-release-webgpu-threads`.
- **What a build makes is in `out/<platform>/<variant>/`**: the library in `lib/`
  (`libwgrender.a`; MSVC's `wgrender.lib`), programs in `bin/`, and on the web the
  whole site in `site/`. The build itself writes all of it (no install or staging
  step), so deleting `out/` is a clean. Its work (CMake's cache and objects, the
  tools' byproducts) is in `build/<preset>/`. `tools/builds.py` is how the tools here
  spell a directory; a new build gets a name that follows these rules.
  `build/` holds nothing else: what a machine sets up once and every build shares (the
  Wine prefix, sokol-shdc, basisu) is in the per-user cache, `tools/hostcache.py`
  (`~/.cache/wgrender`; `WGR_CACHE_DIR` moves it).
- `CMakePresets.json` names every build. This machine's own, `linux-x64-*` or
  `macos-arm64-*`: `-release`, `-debug`, `-debug-headless` (unit tests, guardrails and
  smoke run), `-debug-tsan` / `-debug-asan` / `-debug-ubsan`; on Windows
  `windows-x64-msvc-release`, `-debug`, `-debug-headless` (static CRT).
  `windows-x64-mingw-release` / `windows-x64-mingw-debug-headless` (cross-built with
  MinGW on Linux or macOS and tested under Wine, or gcc on Windows), and the web:
  `wasm32-release`, `wasm32-release-threads`, `wasm32-release-webgpu`,
  `wasm32-release-webgpu-threads`, `wasm32-debug`, `wasm32-debug-threads` (Emscripten,
  exactly `build.json`'s `"emscripten"` version, which CI reads too; another is refused
  unless `WGRENDER_EMSCRIPTEN_VERSION` names it:
  `$EMSDK`, or `emcc` on PATH). Tests run on debug builds. A native platform's presets
  show only on that host. `cmake --preset P && cmake --build
  --preset P`, then `ctest --preset P` where it has tests. Visual Studio and VS Code
  read the presets.
  A program of its own takes the library with `add_subdirectory` and
  `target_link_libraries(... wgrender)`.
- `build.json` — the build as data: the sources, include paths, and per desktop OS and
  web target the defines, flags and link libraries. `CMakeLists.txt` lists none of its
  own, and the bindings read the same file to compile wgrender with their own
  toolchains. Edit it by hand: a new `src/*.c` goes in `sources` (`tools/check_rules.py` fails
  until it does).
- `tools/build_web_library.py [BACKEND=webgpu] [WEB_THREADS=1] [WEB_DEBUG=1]` — the web library
  alone, from `build.json` with emcc and Python only, into the same
  `out/wasm32/<variant>/lib/libwgrender.a` the preset `wasm32-<variant>` makes:
  how a binding builds it with nothing but emsdk. Web builds link at `-O3` unless
  debug (no optimization, assertions, debug info).
- The headless preset's tests (`ctest --preset linux-x64-debug-headless`): `unit` (`tests/unit/`,
  no stubs, no display or GPU; new tests go in `tests/unit/tests.h` and the table in
  `tests/unit/main.c`; add or update tests alongside code changes), `check`
  (`tools/check_rules.py`, through clang: the naming rules below, the public API's shape,
  the module boundary, `build.json`'s sources, the tools), and `smoke.<example>`: every
  example run headless for 180 frames, failing on crashes, timeouts or error logs
  (`tools/run_smoke_test.py`). `tsan` (or `asan`, `ubsan`) runs the unit tests under a
  sanitizer: run `linux-x64-debug-tsan` when touching audio or other code shared with
  the mixer thread.
- **include/ and examples/ stay backend-free, and the compiler holds it:** each public
  header is compiled alone with only `include/` on the path (the `headers_alone` target
  of a test build), and the examples with only `include/`, `examples/` and `deps/clay`.
  A sokol header or identifier in either fails the build.
- **No tool reads source as text.** What a tool needs to know about code comes from
  something that parses it: the public headers from clang (`tools/headers.py`, which
  `check_rules.py` and the binding's generators use), the binding's Haxe from the Haxe
  compiler (`wgr.macros.Members`), a custom shader from sokol-shdc (its parse, `--dump`,
  for the sections; the SPIR-V it compiles through, `tools/spirv.py`, for the
  parameters' names, types and offsets). Matching a program's *output* is fine. Where nothing
  parses it, raise it rather than scan. clang is emsdk's, or one on PATH.
- `tools/setup_system_packages.py [check|install]` — the Linux desktop build's system packages (GL, X11,
  ALSA); a Linux desktop configure runs the check.
- `tools/run_windows_program.py program.exe` — run a Windows build under Wine (wine64/wine, or Steam's
  Proton); the `windows-x64-mingw-debug-headless` preset's tests go through it.
- `tools/setup_mingw.py` — the pinned MinGW-w64 (a WinLibs GCC) the `windows-x64-mingw`
  presets build with on a Windows host: `cmake/mingw-w64.cmake` runs it, and it
  downloads into the per-user cache once. Never the `gcc` on PATH. To move to a newer
  GCC, change its release, URL and SHA-256 together.
- `tools/verify_on_windows.py HOST [--msvc] [--variant PRESET]` — build and test the
  working tree, committed or not, on a real Windows machine over ssh (cmd.exe as its
  shell): copies it to a scratch folder there, runs the presets (MinGW by default,
  `--msvc` for Visual Studio's compiler), and deletes everything after.
- `tools/update_sokol.py [ref]` — update the vendored sokol headers from libwgrender's sokol
  fork (github.com/robknopf/sokol: upstream plus fixes libwgrender needs; sync the fork
  with floooh/sokol there first). Records the fork and upstream commits in
  `deps/sokol/VERSION`.
- `tools/update_clay.py [ref]` — the same for Clay (used only by `examples/clay.c`),
  from libwgrender's fork (github.com/robknopf/clay: upstream plus fixes, each on its own
  branch merged into the fork's `main`). Records both commits in `deps/clay/VERSION`.
- `tools/bench/run_benchmark.py loadbench [--desktop] [--ktx]` — worst frame while loading large
  glTF models on create with an upload budget vs without (downloads them on first use); `--ktx`
  with their textures compressed (made the first time; needs `--desktop`: headless
  samples no compressed format).
- `tools/bench/run_benchmark.py shadowbench [--desktop]` — what a casting light costs a frame: the
  same scene with no shadows, one light at three map sizes, two lights, one where nothing
  receives, one where every model shares a mesh and material (what instancing is worth),
  and two where the camera faces away from everything (with culling on and off, which is
  what frustum culling is worth), at four model counts. Headless is CPU only (no GPU at
  all); `--desktop` opens a window with vsync off for real frame times.
- `tools/bench/run_benchmark.py spritebench [--desktop]` — sprite-heavy scenes (a grid, a
  perspective field with mixed facings, 3D and 2D particles as sprites and from
  emitters): frame time, CPU split into update / scene / submit, sokol_gl vertex/command
  use.
- The benchmarks are targets of the web presets too (`loadbench`, `shadowbench`,
  `spritebench`, `stress`; `benches` for all): pages of their own under `bench/` in the
  site, `/bench/?ex=spritebench` (results in the browser console).
- `python3 tools/check_web_examples.py [--backend=webgpu] [--threads]` — the web build's smoke
  test in a browser, on the matching web preset's site (needs a Chromium-based
  browser: Brave, Chrome, Chromium or Edge, and Python's standard library; WebGPU runs on a virtual X display when Xvfb is installed,
  else in a visible window). Web builds have no threads unless their variant adds
  `-threads`; a threaded build needs cross-origin isolation (`tools/serve_site.py` sends the
  headers).
- `python3 tools/check_asset_cache.py [--manifest] [--backend=webgpu] [--threads]` — the web
  asset cache across visits: tilemap in one browser context while its sheet is kept,
  changed and deleted on the server, and the network blocked; each visit judged by its
  requests' statuses and the screen. `--manifest` does it with manifests. Run both when
  touching `wgr_fs` or the asset fetch.
- `tools/gen_manifest.py DIR` — the asset manifests (`manifest.json` in DIR and every
  directory under it) that `wgr_asset_set_manifest` reads; `tools/build_site.py` runs it.
- `python3 tools/serve_site.py [port] [out/wasm32/<variant>/site]` — the dev server (COOP/COEP headers,
  `/assets/` mounted) on http://localhost:8000. `--tls CERT KEY` serves HTTPS for other
  devices on the LAN (a phone), which need a secure page for threaded builds.
  `--cache --gzip` serves as a real host should (versioned code cached for good; see
  README "Startup and hosting"). `--assets DIR` mounts DIR at `/assets/` instead.
- `tools/build_site.py [out/wasm32/<variant>/site]` — a self-contained copy of a web build,
  assets included, for a static host (the Pages workflow publishes `wasm32-release`'s).
- `python3 tools/measure_example_startup.py [--backend=webgpu] [--threads]` — startup times per web
  example: cold, warm and hot visits, locally and on emulated 4G, from libwgrender's
  `wgr:*` performance marks; `--devtools=PORT --url=URL` measures a phone. Run it when
  touching init, the page shell or web build flags.
- `tools/measure_example_sizes.py [out/wasm32/<variant>/site]` — wasm/JS sizes per web example (raw and gzip;
  brotli if installed).
- `tools/run_benchmarks.py [--doc | --all]` — the C `simple` against every binding
  (docs/benchmarks.md): download size, frame cost, JS heap and GC, and what a call from a
  JS guest costs. Measures the C baseline into `bench/results.json` and collects each
  binding's own `bench/results.json` (`bindings/haxe/`); `--doc` only regenerates the
  page, `--all` also runs each binding's own `tools/run_benchmarks.py` in between. The
  harness is `tools/bench/` (`measure.py`, which bindings import, plus `measure_page.py`:
  frame cost, GC and call counts in the browser; `callbench/`, a page; and `stress.c`, the scene the bindings
  port: `/bench/?ex=stress&n=5000`). The stress runs need Xvfb and a GPU. By hand, not
  CI; commit both files.
- `tools/compress_textures.py [--linear] name.png...` — compressed texture files beside
  each PNG (`name.bc7.ktx`, `.astc.ktx`, `.etc2.ktx`), loaded as `name.ktx`
  (docs/HISTORY.md, "compressed textures"); builds a pinned Basis Universal encoder into the per-user
  cache (`tools/hostcache.py`) the first time. `--gltf model.gltf` does a model's textures and writes
  `model.ktx.gltf`.
- `tools/pack_shader.py name.glsl` — compile a custom material shader (written against
  `shaders/wgr.glsl`) into `name.wgrshader` for every backend. The generators, each a
  CMake target too: `tools/gen_shaders.py --examples` (`gen-example-shaders`) repacks
  `examples/shaders/*.glsl` into the committed `examples/assets/shaders/`; run it after
  changing one of them or `shaders/wgr.glsl`. `tools/gen_shaders.py` (`gen-shaders`)
  regenerates `src/shaders/*.glsl.h`. Both fetch the pinned sokol-shdc into
  the per-user cache (`tools/hostcache.py`) the first time, checked against its SHA-256
  (`tools/shdc.py`; a new pin in `deps/sokol/VERSION` needs its hashes there too).
- `gen-brdf-lut` (a target of a Linux, macOS or Windows preset) — regenerate the baked BRDF table
  (`src/data/wgr_brdf_lut.h`) after changing `wgri_environment_brdf_lut` or its size (a
  unit test fails until you do).
- Run `python3 tools/verify_builds.py` (this machine's release, debug-headless and debug-tsan
  presets, `windows-x64-mingw-release` / `windows-x64-mingw-debug-headless` when MinGW
  and Wine are installed, and the Haxe binding's suite, `bindings/haxe/tools/check_binding.py`,
  when Haxe is) before calling a change done; add `--web` (every example on
  `wasm32-release`, `wasm32-release-threads` and `wasm32-release-webgpu-threads`, loaded
  in the browser, and the Haxe examples built for the web and driven) when touching
  rendering, assets or web code, and `--windows HOST` (MinGW and MSVC built and tested
  on a real Windows machine over ssh, `tools/verify_on_windows.py`) when touching
  threads, files and paths, the platform layer (`wgr_platform.c`, `deps/sokol_utils`) or
  the build.
  Without `--web` nothing links a web example, so **EM_JS changes are unverified until
  an example links** — closure runs then, not when the library is built, and it is what
  catches a typo in the JS body (`$0` is EM_ASM syntax; EM_JS takes named parameters).
  After touching EM_JS link at least one (`cmake --build --preset wasm32-release --target
  hello`).

## Process

- **Ask before changing observable behavior or public API** (`include/*.h`):
  lifecycle, init/run/tick order, what callers may assume. Purely internal
  refactors with no behavioral impact don't need that step.
- For feature work or non-trivial fixes, **outline the plan first** and wait for
  the go-ahead, unless already told to implement.
- **Correct over compatible.** libwgrender is pre-1.0: when the right design or default
  breaks existing code or examples, choose the right one and update the callers.
  Don't add implicit fallbacks just to keep old behavior working. Still ask before
  changing public API or observable behavior (above), but recommend the correct
  option.
- Read-only tasks (questions, reviews) need no approval.
- **Keep the core a plain C library.** Language bindings live here, under `bindings/`
  (`bindings/haxe`), built only on the public API, so a public API change updates them
  in the same commit (`tools/verify_builds.py` runs the Haxe binding's suite when Haxe is
  installed). Scripting hosts and networking beyond asset downloads (WebSockets, HTTP
  APIs, multiplayer) are separate modules/repos built on the public API; don't add
  them here.

## Docs: which one is true

- **`include/*.h` is the contract.** A header comment says what the code does *now*,
  and changes in the same commit the behavior does. It is the one place never allowed
  to lag.
- **`docs/PLAN-*.md` is what's open: a proposal, or the rest of one.** A plan says its
  status and what's left; nothing in it is history. When a phase is built, its record
  -- the design as it was, the decisions, "as built", what was measured -- moves to
  `docs/HISTORY.md`, under the plan's title, in the same commit; a plan with nothing
  left moves there whole and its file goes. A phase dropped rather than built moves
  there too, saying why.
- **`docs/TASKS.md` is what's left to do.** A task done moves to `docs/HISTORY.md`
  ("Tasks done", under its section) in the commit that does it, rather than being
  ticked in place. A task dropped moves there too ("Tasks dropped"), saying why: the
  reason is what stops it being proposed again. Nothing leaves TASKS or a plan
  without a record.
- **`docs/HISTORY.md` is the record, never current.** It keeps text as it was written
  -- a name or a path in it may since have changed -- and isn't rewritten when later
  work supersedes it. Read it for *why* things are the way they are, and for what was
  already tried.
- **`docs/ARCHITECTURE.md` describes the design as it is now**, and changes with it.
- So: for current behavior read the header and the code. When a header and the history
  disagree, the history is old; when a header and the code disagree, fix the header --
  that disagreement is the bug.
- **Say clamp or refuse, and mean it.** Clamp when every value in range is the same
  request at a different fidelity (a corner radius, a segment count, a map size);
  refuse -- return false -- when the value would change what the program asked for or
  has no meaning (an emitter's particle cap, a zero extent, an unknown parameter). A
  setter's comment uses the word that matches the code, and "false for ..." names
  every refusal. That sentence is a binding's only account of what the `bool` means:
  wgrender-hx went to flat statics partly because a property setter structurally
  cannot return it, so the refusal now always reaches the caller and the word had
  better be true. "Capped" on a setter that refuses is a wrong promise downstream.
- **Sweep the headers when a phase lands.** Whatever a plan's `Status:` line gains,
  re-read that subsystem's header in the same commit: a limit that grew, a case that
  used to be refused, a "for now" that stopped being true. `include/` is 34 files and
  ~2500 lines, so a full sweep is an afternoon's reading at worst -- worth doing
  whenever several phases have landed since the last one. The shadow comment that
  claimed one directional caster when four lights and spots already worked is what
  this rule is for.

## Resource / Object model

libwgrender layers everything loadable as **Asset → Resource → Object** (full detail in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)):

- **Resource** = the *data noun* — loaded, reference-counted, deduped by source
  path, shared. Holds CPU-side data the game needs (e.g. pick geometry, alpha mask).
- **Object** = the *placed/heard noun* — a lightweight handle-pooled instance that
  references a resource via `set_<resource>()` and owns its own transform / tint /
  playback state.

The pairings are deliberately different words so the name signals the category:
`Texture → Sprite`, `Mesh → Model`, `Audio → Sound`, `Font → Text`.

## Public API shape (hard rules)

The public surface (`include/*.h`) is **language/bindings-agnostic**: every
parameter and return value is a **handle (`wgr_handle_t`)**, an **integral / float
/ enum**, a **`const char *`** (paths and text), or a **fixed-layout math value by
value: `vec2_t`, `vec3_t`, `vec4_t`, `quat_t`** (a quaternion is laid out as a
`vec4_t`). **No other pointers in user code** — never `unsigned char *data` /
`int size`, never struct pointers, no callbacks, no `...` — **and never a record.**
This is enforced by `tools/check_rules.py` (the `check` test), not just convention,
with two lists beside it as for getters: `TYPES_EXEMPT`, a call that breaks the rule on
purpose and why (the loop setters: the platform owns the loop, so it calls the
program), and `TYPES_TODO`, the known gaps, which only shrinks.

Why the math values and nothing else: a struct returned by value puts its layout in
the contract, which every binding mirrors and every FFI must get right (SysV returns a
`vec3_t` in two registers, Win64 through a hidden pointer, and wasm always through
one, which a JS guest reads back out of the heap). That is worth paying once for a type
whose layout can never change -- `vec3_t` will not grow a field -- and not for a record
that can: the day `wgr_pick_result_t` gains a surface normal, every binding's copy is
wrong, some silently. So the test is "can its layout ever change?", not size.
`matrix_t` passes it, but no public call takes or returns one; it joins the list the
day one needs to. Handles are unaffected either way: a value copied out has no
lifetime, so it can't dangle, alias or outlive anything. Records returned today
(`wgr_pick_result_t`, `wgr_pick_stats_t`, `wgr_touch_t`, `wgr_touch_gesture_t`,
`wgr_mouse_state_t`, `wgr_keyboard_state_t`) are known gaps, listed in
`tools/check_rules.py`'s `RECORDS_TODO`: each becomes per-field getters, or a handle to the
result, as its subsystem is next worked on.

Creation follows one pattern, no exceptions:

- **Resource** ← created from a **path** (or a generator): `wgr_texture_create(path)`,
  `wgr_mesh_create(path)`, `wgr_audio_create(path)`, `wgr_font_create(path)`,
  `wgr_mesh_create_cube(...)`.
- **Object** ← created from a **resource handle**, never a path:
  `wgr_sprite3d_create(texture)`, `wgr_model_create(mesh)`, `wgr_sound_create(audio)`,
  `wgr_text2d_create(font)`.
- Bare `_create` for both — the **noun** says which (resource noun → path, object
  noun → handle). **No** `_create_from_memory` and **no** "create object from
  file" shortcut; loading bytes and turning them into a resource is internal.
- **Freeing says which layer it is:** resources are reference counted and shared,
  so they're released with `wgr_resource_release(handle)` — it drops this handle's
  reference and frees the resource only when the last one goes. Objects are private,
  so they have `wgr_<object>_destroy(handle)`. `retain` stays internal: one `create`
  is one reference.
- **What every resource shares is the resource section's** (`wgr_resource.h`,
  `src/wgr_resource.c`): status, the file it was read from, release, and inside,
  reference counting, finding one by its path and load on create. A resource
  module's records start with a `wgri_resource_t` and it registers its pool
  (`wgri_resource_register`); it keeps only what is its own, and its loader.

**Every value a setter stores has a getter.** `set_<value>` pairs with
`get_<value>` (or `is_`/`has_` for a bool), or with one getter per value when a setter
takes several (`set_spot_cone` -> `get_spot_inner_angle`, `get_spot_outer_angle`), and a getter reads
0 for a handle that isn't one. A setter that takes a vector's components returns them
as that vector: `set_pivot(x, y)` -> `vec2_t get_pivot`. It is what makes a clamp observable: the
light getters came from `set_shadow_map_size(64)` answering true while nothing outside
could learn the map was 256. The transform rule under Naming is this rule applied to a
transform's parts. `tools/check_rules.py` holds it, with two lists beside it:
`GETTERS_EXEMPT`, a setter with no getter on purpose and why (a callback, an action like
`set_manifest`, a shape's geometry maker), and `GETTERS_TODO`, the gaps known when the
rule was written. A new setter needs its getter or an entry in the first; the second
only shrinks, since the check fails when a listed setter gains its getter.

**A resource loads on create** (libwgt's model, `include/wgr_resource.h`):
`wgr_*_create(path)` takes an asset path, returns the handle at once, PENDING, and the
asset layer makes the file local, prepares and fills it in; it is READY or FAILED in
a later frame. Nothing is called back: objects take a resource in any status and do
the right thing until it's READY, and a program reads `wgr_resource_get_status` for
what it wants to show. *Ensuring* a file only makes it local. Bytes never cross into
user code.

## Bindings: one name per C call

A binding names things in its own language's style -- `Model.create(mesh)` in Haxe,
`newModel(mesh)` in Nim -- and nothing requires reading the C name off the binding's.
What every binding keeps is the correspondence:

1. **Each C function has exactly one public name.** Overloads of that name count as
   one: Nim's `newModel(mesh)` and `newModel()`, or Haxe's two
   `Shape2D.drawRoundedRectangle`s, one with a radius and one with four corners.
2. **A public member that calls C calls one C function.** Anything that combines calls
   -- a version string built from major, minor and patch -- calls the members that
   wrap them, never C directly.
   A generic that picks its one call by type at compile time (Nim's
   `when e is Emitter3d`) is overloads written once, and counts as one call.
3. **Sugar is welcome, on top of those members.** Constructors, operators, extension
   methods and language features that add no member (Haxe's `@:using`, Nim's UFCS)
   make a binding pleasant to use; they reach C only through the one name.
4. **Private plumbing is exempt:** a callback trampoline, or a helper shared by `on` and
   `once`.

Why: a C call's doc -- above all its "false for ..." refusal sentence (see "Say clamp
or refuse") -- then has exactly one home in each binding, a binding can be audited for
coverage call by call, and a second path to C can't quietly skip a check the first one
makes. Each binding's `tools/check_coverage.py --check` enforces rules 1 and 2.

## Core and optional subsystems

- Optional subsystems (textures, models, sprites, particles, audio, ...) register with
  `WGRI_MODULE` (`src/internal/wgr_module_internal.h`), so a program links only what it uses. The
  core (`wgr.c`, `wgr_render`, `wgr_scene`, ...) never calls them by name: add a module
  callback or a hook (`wgri_render_hooks`, `wgri_scene_hooks`) instead.
  `tools/check_rules.py` enforces it. Details: ARCHITECTURE.md §7b.

## Shaders

- Authored once in `src/shaders/*.glsl`; `tools/gen_shaders.py` regenerates the committed
  `*.glsl.h` for every backend. Read the **generated** `glsl300es`, not just what you
  wrote: shdc flattens a uniform block to one `uniform vec4 name[N]`, and GLES drivers
  are strictest about how that array is indexed.
- **Index a flattened uniform array at a constant, unconditionally.** Adreno's compiler
  clamps a dynamic index only when it can bound it: a divided index (`arr[i / 4]`) and a
  *branch* around the read both defeat it, and a ternary is a branch once shdc is done
  with it. It fails the link with `cannot compute gv size for oob` -- a driver assertion,
  not a limit -- and the draw silently produces nothing. Read every candidate and select
  arithmetically (`mix(lo, hi, step(...))`). An affine `arr[i + k]` and a dynamic vector
  component (`v[i % 4]`) are both fine. `src/shaders/wgr_sprite.glsl`'s `curve_key` is
  the worked example.
- A shader that only *some* GPUs reject won't show up in `tools/verify_builds.py --web`
  or CI. Link-check on a real low-end device when you touch one.

## Naming

Family-wide rules (repo names, prefixes, where `lib` goes, ownership, vendoring) live
in [whirlinggizmo/.github/CONVENTIONS.md](https://github.com/whirlinggizmo/.github/blob/main/CONVENTIONS.md).
What they come to here:

- **Project:** `wgrender` — repo `wgrender-c`, in the whirlinggizmo org. In prose,
  "libwgrender" where it needs distinguishing from the `wg-renderer` app, plain
  "wgrender" otherwise.
- **Artifact:** `libwgrender.a` (every target directory). This is the *only* place
  `lib` appears, and it isn't a choice: `-lwgrender` resolves to `libwgrender.a`. A
  future MSVC/DLL target would be `wgrender.dll` + `wgrender.lib` (MinGW keeps
  `libwgrender.a` / `libwgrender.dll.a`, since it uses ld).
- **Macros:** `WGR_` public, `WGRI_` internal, as for functions. The exception is a
  **build flag** the build system also passes: `-DWGR_HEADLESS` and `#ifdef
  WGR_HEADLESS` have to spell it the same, so build flags stay `WGR_` wherever they
  are used.
- **A script is `<verb>_<noun>`: what it does, and to what.** `gen_` writes committed
  files, `check_` fails on what's wrong, `run_`, `build_`, `measure_`, `setup_`,
  `update_`, `verify_`... (`verify_builds.py`, `check_web_examples.py`,
  `measure_example_sizes.py`); a bare verb doesn't say to what. **A module, imported and
  never run, is one word** (`builds.py`, `browser.py`, `shdc.py`), and a script is never
  imported: what scripts share goes in a module. The same in `bindings/`. Every script
  takes `--help` (its docstring, and nothing else done) and stops on an argument it
  doesn't take: `tools/cli.py`'s `parse`, first thing in its `__main__` block.
  `tools/check_rules.py` holds all of it (imports read with Python's `ast`); a new
  module goes in its `TOOL_MODULES`.
- **Tooling environment variables:** `WGRENDER_` (`WGRENDER_WEB_PROFILE`). They aren't
  library symbols, and three letters collide too easily in a process environment. A
  variable naming another project takes *that* project's name (`SOKOL_DIR` for a sokol checkout, because
  sokol is what sokol is called).
- **Sibling repos:** `wgutils-c` and friends follow the same pattern — see CONVENTIONS.md
  before naming anything new.
- **Prefix says which surface it is:** `wgr_` is public, `wgri_` is internal. A call
  site reads as what it is without looking anything up, and `tools/check_rules.py` can enforce
  it, which it can't when one prefix covers both.
- **Public API** (`include/*.h`): subsystem-first `wgr_<section>_<action>`.
- **Predicates say which kind of question they answer.** `is_<state>` is what it is
  right now (`wgr_window_is_fullscreen`, `wgr_light_is_enabled`); `has_<noun>` is that
  a feature exists here at all (`wgr_has_threads`, `wgr_window_has_fullscreen`);
  `can_<verb>` is that an action is possible (`can_move` in `wgr_platform.c`). The noun
  vs verb is what picks the last two: "has fullscreen" reads, "can fullscreen" doesn't,
  and "can move window" reads where "has move" doesn't. All three return `bool`, take
  no state with them, and a binding carries the verb straight through --
  `wgr_window_is_fullscreen` is `Window.isFullscreen` in wgrender-hx and `isFullscreen`
  in wgrender-nim.
  The verb is the name in every language, not a hint someone translates, which is why
  it is worth getting right. `tools/check_rules.py` doesn't enforce verbs (it checks
  types, `_ptr` and the prefix per surface), so this is convention.
- **Every kind with a transform has the same calls for it.** For each part it has
  (position, rotation, scale): `set_<part>`, which leaves the other parts as they are,
  and `get_<part>`; with more than one part, also `set_transform` for them all in one
  call, the cheapest per-frame path for a binding that crosses a boundary per call. 3D
  parts are three floats in and a `vec3_t` out (rotation in radians); 2D ones are a
  position and scale of two floats and a `vec2_t`, and one angle. A getter reads 0 for a
  handle that isn't one. So a caller moving something never has to know, or keep, the
  parts it isn't changing. `tests/unit/transform_test.c` checks every kind; a new kind
  with a transform goes there too.
- **Cross-`.c` internals** (one `src/*.c` calling another's symbol): `wgri_<subsystem>_…`,
  declared **only** in `src/internal/*_internal.h` — promoting one to `include/` is a
  rename to `wgr_`, which is the point: the contract changed. The file suffix keeps
  basenames unique: 17 subsystems have both a
  public and an internal header, and without it a quoted `#include "wgr_texture.h"`
  from inside `src/internal/` finds the sibling instead of the public one — which is
  why those includes used to need angle brackets and a comment each.
- **File-local `static`** helpers: no prefix at all; `verb_noun` in `snake_case`;
  shortest name that's unambiguous in the file. Prefer `resolve_*` / `lookup_*` for
  handle→pointer helpers and `is_*` / `has_*` for predicates.
- **Types:** `wgri_<noun>_t` internally, `wgr_<noun>_t` for the few public ones — no `_data`/`_instance` suffix. The noun carries the
  layer: resource (`wgri_texture_t`, `wgri_mesh_t`) vs object (`wgri_sprite3d_t`,
  `wgri_model_t`). Handle kinds live in `include/wgr_handle.h`.
- **Resolved instance pointers:** a local/param holding a raw `wgri_<noun>_t *` that
  was resolved from a `wgr_handle_t` is named `<noun>_ptr` (e.g.
  `wgri_model_t *model_ptr = resolve(handle);`). This keeps the **pointer path**
  visually distinct from the **handle path** at every call site. Don't add `_ptr`
  redundantly where no handle coexists (pure-pointer helpers, value locals).

## Scripted renames

When a `static` helper's old name is a prefix of a longer `wgr_*` symbol in the same
file, use **whole-identifier** (word-boundary) replacement, never blind substring
replace, or you'll corrupt the public API. Guard against matching inside comments
(possessives) and reused short names (loop counters, value structs).
