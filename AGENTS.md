# AGENTS

Conventions for working in **libwgrender** (a sokol-based C library, evolved from librl).
Keep this file short and rule-shaped. The authoritative design doc is
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build & verify

- `make` — build the static library (`build/linux/libwgrender.a`; `build/macos` on a Mac).
  Build outputs live in one directory per target, named for the OS or backend they are
  for: libraries in `build/{linux,linux-headless,windows,windows-headless,webgl2,webgpu}`
  (`<backend>-nothreads` for `WEB_THREADS=0`), programs and web sites in
  `examples/build/<target>`.
- `make web [BACKEND=webgpu] [WEB_THREADS=0] [WEB_DEBUG=1]` — the web library
  (`build/<backend>/libwgrender.a`; `-nothreads`/`-debug` suffixes). Web builds link at
  `-O3` unless `WEB_DEBUG=1` (no optimization, assertions, debug info). Web
  settings live in `mk/web.mk`, shared with the
  examples, which link it. `make print-web-flags` prints what a program needs to
  compile and link against it.
- `make examples` — build everything in `examples/`.
- `make check` — guardrails; currently enforces that `include/` and `examples/`
  stay **backend-free** (no sokol/GL leakage into the public surface).
- `make test` — unit tests (`tests/unit/`, link against `build/linux-headless/libwgrender.a`, no
  stubs, no display or GPU).
- `make test SANITIZE=thread` (or `address`, `undefined`) — the unit tests with the
  library built in under a sanitizer. Run `thread` when touching audio or other
  code shared with the mixer thread.
- `make smoke` — every example built headless (`make HEADLESS=1`) and run for 180
  frames; fails on crashes, timeouts or error logs. Needs no display. Add or update tests alongside code changes; new tests go in
  `tests/unit/tests.h` and the table in `tests/unit/main.c`.
- `make windows` — the library and examples cross-compiled for Windows with MinGW
  (`build/windows`, `examples/build/windows/*.exe`; `WINDOWS=1` on any target).
  `make windows-test` / `make windows-smoke` run the unit tests and the headless
  examples under Wine (`tools/wine.sh`: wine64/wine, or Steam's Proton). `make verify`
  builds `windows` when MinGW is installed.
- `tools/update_sokol.sh [ref]` — update the vendored sokol headers from libwgrender's sokol
  fork (github.com/robknopf/sokol: upstream plus fixes libwgrender needs; sync the fork
  with floooh/sokol there first). Records the fork and upstream commits in
  `deps/sokol/VERSION`.
- `tools/update_clay.sh [ref]` — the same for Clay (used only by `examples/clay.c`),
  from libwgrender's fork (github.com/robknopf/clay: upstream plus fixes, each on its own
  branch merged into the fork's `main`). Records both commits in `deps/clay/VERSION`.
- `make loadbench [DESKTOP=1] [KTX=1]` — worst frame while loading large glTF models in
  the background vs synchronously (downloads them on first use); `KTX=1` with their
  textures compressed (made the first time; needs `DESKTOP=1`: headless samples no
  compressed format).
- `make shadowbench [DESKTOP=1]` — what a casting light costs a frame: the same scene
  with no shadows, one light at three map sizes, two lights, one where nothing
  receives, one where every model shares a mesh and material (what instancing is worth),
  and two where the camera faces away from everything (with culling on and off, which is
  what frustum culling is worth), at four model counts. Headless is CPU only (no GPU at all); `DESKTOP=1`
  opens a window with vsync off for real frame times. `make shadowbench-web` builds it
  as a page, `/bench/?ex=shadowbench`.
- `make spritebench [DESKTOP=1]` — sprite-heavy scenes (a grid, a perspective field
  with mixed facings, 3D and 2D particles as sprites and from emitters): frame time,
  CPU split into update / scene / submit, sokol_gl vertex/command use.
  `make spritebench-web` builds it as a page, `/bench/?ex=spritebench` (results in the
  browser console).
- `make webcheck [BACKEND=webgpu] [WEB_THREADS=0]` — web build smoke test in a
  browser (needs Emscripten, Node >= 22, a Chromium-based browser; WebGPU runs on a
  virtual X display when Xvfb is installed, else in a visible window). Web builds use
  threads by default, which need cross-origin isolation (`tools/serve.py` sends the
  headers); `WEB_THREADS=0` builds without.
- `make serve [BACKEND=webgpu] [WEB_THREADS=0]` — the dev server (`tools/serve.py`:
  COOP/COEP headers, `/assets/` mounted) on http://localhost:8000. `make serve-tls`
  serves HTTPS on 8443 for other devices on the LAN (a phone), which need a secure page
  for threaded builds; it takes `TLS_CERT` / `TLS_KEY` (a certificate they trust).
  `serve.py --cache --gzip` serves as a real host should (versioned code cached for
  good; see README "Startup and hosting").
- `make webstart [BACKEND=webgpu] [WEB_THREADS=0]` — startup times per web example
  (`tools/webstart.mjs`): cold, warm and hot visits, locally and on emulated 4G,
  from libwgrender's `wgr:*` performance marks; `WEBSTART_FLAGS="--devtools=PORT --url=URL"`
  measures a phone. Run it when touching init, the page shell or web build flags.
- `tools/benchmarks.py [--doc]` — the C `simple` against every binding
  (docs/benchmarks.md): download size, frame cost, JS heap and GC, and what a call from a
  JS guest costs. Measures the C baseline into `bench/results.json` and collects each
  sibling binding's own `bench/results.json`; `--doc` only regenerates the page. The
  harness is `tools/bench/` (`measure.py`, which bindings import, plus `bench.mjs`,
  `gcbench.mjs`, `callcount.mjs`, `callbench/`). By hand, not CI; commit both files.
- `tools/compress_textures.sh [--linear] name.png...` — compressed texture files beside
  each PNG (`name.bc7.ktx`, `.astc.ktx`, `.etc2.ktx`), loaded as `name.ktx`
  (docs/PLAN-textures.md); builds a pinned Basis Universal encoder into `build/tools`
  the first time. `--gltf model.gltf` does a model's textures and writes
  `model.ktx.gltf`.
- `tools/shaderpack.py name.glsl` — compile a custom material shader (written against
  `shaders/wgr.glsl`) into `name.wgrshader` for every backend (needs `tools/sokol-shdc`).
  `make example-shaders` repacks `examples/shaders/*.glsl` into the committed
  `examples/assets/shaders/`; run it after changing one of them or `shaders/wgr.glsl`.
- `make brdf-lut` — regenerate the baked BRDF table (`src/data/wgr_brdf_lut.h`) after
  changing `wgri_environment_brdf_lut` or its size (a unit test fails until you do).
- `make websize [BACKEND=webgpu] [WEB_THREADS=0]` — wasm/JS sizes per web example
  (raw and gzip; brotli if installed), also summarized after `make wasm-all`.
- Run `make verify` (lib + examples + `make check` + `make test` + `make smoke`,
  about 15 s) before calling a change done; run `make webcheck` (and
  `BACKEND=webgpu`) too when touching rendering, assets or web code. `make verify`
  never links a web example, so **EM_JS changes are unverified until an example
  links** — closure runs then, not when the library is built, and it is what catches
  a typo in the JS body (`$0` is EM_ASM syntax; EM_JS takes named parameters). After
  touching EM_JS run at least `make -C examples wasm WASM_EXAMPLE=hello`, and
  `make windows-test` and `make windows-smoke` (under Wine) when touching threads,
  files and paths, the platform layer (`wgr_platform.c`, `deps/sokol_utils`) or the
  build.

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
- **Keep the core a plain C library.** Scripting hosts, language bindings and
  networking beyond asset downloads (WebSockets, HTTP APIs, multiplayer) are separate
  modules/repos built on the public API; don't add them here.

## Docs: which one is true

- **`include/*.h` is the contract.** A header comment says what the code does *now*,
  and changes in the same commit the behavior does. It is the one place never allowed
  to lag.
- **`docs/PLAN-*.md` is a proposal plus its own history.** The `Status:` line at the
  top is current; everything under it -- "Proposed design", the API sketch, "Phase N as
  built" -- records what was thought or shipped at the time and is *not* rewritten when
  later work supersedes it. A stale-looking line inside "Phase 1 as built" is accurate
  as history: update the Status line, don't edit the record.
- **`docs/TASKS.md` is a checklist; a ticked box is history** -- its text describes what
  was true when it was ticked, not necessarily now.
- So: for current behavior read the header and the code. Read a plan for *why*, and for
  what was already tried. When a header and a plan disagree, check the code and fix the
  header -- that disagreement is the bug, not the plan.
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
/ enum**, or a **`const char *`** (paths and text). **No other pointers in user
code** — never `unsigned char *data` / `int size`, never struct pointers. This is
enforced by `tools/check_naming.sh` (run by `make check`), not just convention.

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
  so they have `wgr_<resource>_release(handle)` — it drops this handle's reference
  and frees the resource only when the last one goes. Objects are private, so they
  have `wgr_<object>_destroy(handle)`. `retain` stays internal: one `create` is one
  reference.

Loading is split from creation (the librl model): the **asset** layer *ensures a
file is local* and fires a **path-only** callback
(`wgr_asset_callback_fn(const char *path, void *user)`); the consumer then calls
the sync `wgr_*_create(path)`. Bytes never cross into user code.

## Core and optional subsystems

- Optional subsystems (textures, models, sprites, particles, audio, ...) register with
  `WGRI_MODULE` (`src/internal/wgr_module_internal.h`), so a program links only what it uses. The
  core (`wgr.c`, `wgr_render`, `wgr_scene`, ...) never calls them by name: add a module
  callback or a hook (`wgri_render_hooks`, `wgri_scene_hooks`) instead. `make check`
  enforces it (`tools/check_modules.sh`). Details: ARCHITECTURE.md §7b.

## Shaders

- Authored once in `src/shaders/*.glsl`; `make shaders` regenerates the committed
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
- A shader that only *some* GPUs reject won't show up in `make verify`, `make webcheck`
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
- **Tooling environment variables:** `WGRENDER_` (`WGRENDER_WEB_PROFILE`). They aren't
  library symbols, and three letters collide too easily in a process environment. A
  variable naming another project takes *that* project's name (`SOKOL_DIR` for a sokol checkout, because
  sokol is what sokol is called).
- **Sibling repos:** `wgutils-c` and friends follow the same pattern — see CONVENTIONS.md
  before naming anything new.
- **Prefix says which surface it is:** `wgr_` is public, `wgri_` is internal. A call
  site reads as what it is without looking anything up, and `make check` can enforce
  it, which it can't when one prefix covers both.
- **Public API** (`include/*.h`): subsystem-first `wgr_<section>_<action>`.
- **Predicates say which kind of question they answer.** `is_<state>` is what it is
  right now (`wgr_window_is_fullscreen`, `wgr_light_is_enabled`); `has_<noun>` is that
  a feature exists here at all (`wgr_has_threads`, `wgr_window_has_fullscreen`);
  `can_<verb>` is that an action is possible (`can_move` in `wgr_platform.c`). The noun
  vs verb is what picks the last two: "has fullscreen" reads, "can fullscreen" doesn't,
  and "can move window" reads where "has move" doesn't. All three return `bool`, take
  no state with them, and a binding carries the name straight through -- wgrender-hx
  mirrors C names mechanically, so `wgr_window_is_fullscreen` is `Window.isFullscreen`.
  The verb is the name in every language, not a hint someone translates, which is why
  it is worth getting right. `tools/check_naming.sh` doesn't enforce verbs (it checks
  types, `_ptr` and the prefix per surface), so this is convention.
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
