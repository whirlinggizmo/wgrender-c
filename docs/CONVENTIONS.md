# Conventions

The rules for working in **libwgrender** (a sokol-based C library, evolved from librl),
for developers and agents alike. Every rule lives here, once; [ARCHITECTURE.md](ARCHITECTURE.md)
is the design as it is now, and says how a rule is met rather than restating it.
[AGENTS.md](../AGENTS.md) adds only what an agent needs on top (see "Docs").

## Scope

- **Correct over compatible.** libwgrender is pre-1.0: when the right design or default
  breaks existing code or examples, choose the right one and update the callers.
  Don't add implicit fallbacks just to keep old behavior working. A change to the public
  API (`include/*.h`) or to observable behavior (lifecycle, init/run/tick order, what
  callers may assume) is agreed before it's made, recommending the correct option.
- **Keep the core a plain C library.** Language bindings live here, under `bindings/`
  (`bindings/haxe`, `bindings/js`), built only on the public API, so a public API change
  updates them in the same commit (`tools/verify_builds.py` runs the Haxe binding's
  suite when Haxe is installed). Scripting hosts and networking beyond asset downloads
  (WebSockets, HTTP APIs, multiplayer) are separate modules/repos built on the public
  API; don't add them here.

## Build and verify

[BUILDING.md](../BUILDING.md) is how to build: what to install, the presets and their
names, where a build's output goes, the web, Windows, the tools and the generated files.
What every build and change keeps:

- **One build system on every host: CMake (3.21+) and Python 3.** No make, no shell
  script, so everything works the same on Windows, Linux and macOS.
- **A build is a preset, named as BUILDING.md names them** (`<platform>-<variant>`, a
  name only ever adding: `-threads`, never `-nothreads`); a new build gets a name that
  follows the same rules, and `tools/builds.py` is how the tools spell its directories.
- **Programs read what a build makes from `out/<platform>/<variant>/`, never from
  `build/`.** `build/<preset>/` holds a build's work and nothing else, so deleting `out/`
  is a clean. What a machine sets up once for every build (the Wine prefix, sokol-shdc,
  basisu) is in the per-user cache, `tools/hostcache.py`.
- **`build.json` is the build as data**: the sources, include paths, and per desktop OS
  and web target the defines, flags and link libraries. `CMakeLists.txt` lists none of
  its own, and the bindings read the same file. Edit it by hand: a new `src/*.c` goes in
  `sources` (`tools/check_rules.py` fails until it does).
- **Toolchains are pinned, by absolute path, and checked**: Emscripten exactly as
  `build.json`'s `"emscripten"` says (CI reads it too; another is refused unless
  `WGRENDER_EMSCRIPTEN_VERSION` names it, and that is announced); the MinGW-w64 GCC
  `tools/setup_mingw.py` installs, never the `gcc` on `PATH` (a newer one changes its
  release, URL and SHA-256 together); sokol-shdc and basisu by SHA-256 (a new shdc pin in
  `deps/sokol/VERSION` needs its hashes in `tools/shdc.py`).
- **Warnings are errors** in every preset (`-Werror`, `/WX`). A project building
  wgrender with `add_subdirectory` doesn't get that unless it asks (`-DWGR_WERROR`).
- **MSVC builds use the static C runtime** (`/MT`, `/MTd`), as every wg* library does.
- **`include/` and `examples/` stay backend-free, and the compiler holds it:** each
  public header is compiled alone with only `include/` on the path (the `headers_alone`
  target of a test build), and the examples with only `include/`, `examples/` and
  `deps/clay`. No sokol header or identifier (`sapp_`, `sg_`, `sgl_`, `saudio_`, ...)
  appears in either; all backend linkage lives in `build.json`. The word "sokol" in a
  prose comment is fine.
- **No tool reads source as text.** What a tool needs to know about code comes from
  something that parses it: the public headers from clang (`tools/headers.py`, which
  `check_rules.py` and the bindings' generators use), the Haxe binding's Haxe from the
  Haxe compiler (`wgr.macros.Members`), a custom shader from sokol-shdc (its parse,
  `--dump`, for the sections; the SPIR-V it compiles through, `tools/spirv.py`, for the
  parameters' names, types and offsets). Matching a program's *output* is fine. Where
  nothing parses it, raise it rather than scan. clang is emsdk's, or one on PATH.
- **Tools are Python, standard library only**: nothing to `pip install`, and no Node.
- **Tests go with the code they test.** Tests run on debug builds. The headless
  preset's `ctest` runs `unit` (`tests/unit/`: no stubs, no display or GPU; a new test
  goes in `tests/unit/tests.h` and the table in `tests/unit/main.c`), `check`
  (`tools/check_rules.py`: the naming rules, the public API's shape, the module
  boundary, `build.json`'s sources, the tools) and `smoke.<example>` (every example run
  headless for 180 frames, failing on a crash, a timeout or an error log). A change adds
  or updates its tests.
- **Generated files are committed, and regenerated in the commit that changes what
  they come from** (BUILDING.md, "Generated files"); a public header's change, comments
  included, regenerates both bindings.
- **Before calling a change done, `python3 tools/verify_builds.py` passes**: this
  machine's release, debug-headless and debug-tsan presets, the MinGW presets when MinGW
  and Wine are installed, and the Haxe binding's suite when Haxe is. Add:
  - `--web` (every example on `wasm32-release`, `-release-threads` and
    `-release-webgpu-threads` in a browser, and the Haxe examples built for the web and
    driven) when touching rendering, assets or web code;
  - `--windows HOST` (MinGW and MSVC on a real Windows machine) when touching threads,
    files and paths, the platform layer (`wgr_platform.c`, `deps/sokol_utils`) or the
    build;
  - `linux-x64-debug-tsan` when touching audio or other code shared with the mixer
    thread;
  - `tools/check_asset_cache.py`, with and without `--manifest`, when touching `wgr_fs`
    or the asset fetch; `tools/measure_example_startup.py` when touching init, the page
    shell or web build flags.
  Without `--web` nothing links a web example, so **EM_JS changes are unverified until
  an example links**: closure runs then, not when the library is built, and it is what
  catches a typo in the JS body (`$0` is EM_ASM syntax; EM_JS takes named parameters).
  After touching EM_JS, link at least one (`cmake --build --preset wasm32-release
  --target hello`).

## Docs: which one is true

- **Every rule lives in a developer doc, once.** This file holds the rules; ARCHITECTURE,
  the READMEs and the plans point to them, and describe how a rule is met without
  restating it. The developer docs are complete without AGENTS.md: it links to rules
  and adds agent-only practice, and never restates or contradicts one. Anything there a
  developer also needs moves here.
- **`include/*.h` is the contract.** A header comment says what the code does *now*,
  and changes in the same commit the behavior does. It is the one place never allowed
  to lag.
- **`docs/PLAN-*.md` is what's open: a proposal, or the rest of one.** A plan says its
  status and what's left; nothing in it is history. When a phase is built, its record
  -- the design as it was, the decisions, "as built", what was measured -- moves to
  `docs/HISTORY.md`, under the plan's title, in the same commit; a plan with nothing
  left moves there whole and its file goes. A phase dropped rather than built moves
  there too, saying why.
- **Carried into libwgt.** libwgrender is converging into libwgt, which carries every
  item that was open in the ROADMAP, TASKS and the plans (libwgt 17f3f18, cb5dee5);
  where each went is in HISTORY.md, "Carried into libwgt". Carried items stay listed
  here, and move out under the rules above, while wgrender is maintained: each of those
  files says so in one line, pointing here.
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
[ARCHITECTURE.md](ARCHITECTURE.md)):

- **Resource** = the *data noun* — loaded, reference-counted, deduped by source
  path, shared. Holds CPU-side data the game needs (e.g. pick geometry, alpha mask).
- **Object** = the *placed/heard noun* — a lightweight handle-pooled instance that
  references a resource via `set_<resource>()` and owns its own transform / tint /
  playback state.

The pairings are deliberately different words so the name signals the category:
`Texture → Sprite`, `Mesh → Model`, `Audio → Sound`, `Font → Text`.

## Public API shape

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
5. **Each binding repeats every refusal sentence** a header gives (its "false for ..."),
   in the docs of the member that makes the call: the Haxe binding's
   `tools/check_refusals.py --check` reads each refusal from the C through clang and
   fails when a member's docs leave one out.

Why: a C call's doc -- above all its "false for ..." refusal sentence (see "Say clamp
or refuse") -- then has exactly one home in each binding, a binding can be audited for
coverage call by call, and a second path to C can't quietly skip a check the first one
makes. Each binding's `tools/check_coverage.py --check` enforces rules 1 and 2, and
the Haxe binding's `check_refusals.py` rule 5.

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
