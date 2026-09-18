# AGENTS

Conventions for working in **libsk** (a sokol-based C library, evolved from librl).
Keep this file short and rule-shaped. The authoritative design doc is
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build & verify

- `make` — build the static library (`build/desktop/libsk.a`). Build outputs live
  in one directory per target: libraries in `build/{desktop,headless,webgl2,webgpu}`
  (`<backend>-nothreads` for `WEB_THREADS=0`), programs and web sites in
  `examples/build/<target>`.
- `make web [BACKEND=webgpu] [WEB_THREADS=0] [WEB_DEBUG=1]` — the web library
  (`build/<backend>/libsk.a`; `-nothreads`/`-debug` suffixes). Web builds link at
  `-O3` unless `WEB_DEBUG=1` (no optimization, assertions, debug info). Web
  settings live in `mk/web.mk`, shared with the
  examples, which link it. `make print-web-flags` prints what a program needs to
  compile and link against it.
- `make examples` — build everything in `examples/`.
- `make check` — guardrails; currently enforces that `include/` and `examples/`
  stay **backend-free** (no sokol/GL leakage into the public surface).
- `make test` — unit tests (`tests/unit/`, link against `build/headless/libsk.a`, no
  stubs, no display or GPU).
- `make test SANITIZE=thread` (or `address`, `undefined`) — the unit tests with the
  library built in under a sanitizer. Run `thread` when touching audio or other
  code shared with the mixer thread.
- `make smoke` — every example built headless (`make HEADLESS=1`) and run for 180
  frames; fails on crashes, timeouts or error logs. Needs no display. Add or update tests alongside code changes; new tests go in
  `tests/unit/tests.h` and the table in `tests/unit/main.c`.
- `make parity` — librl → libsk parity report (needs `../librl`).
- `tools/update_sokol.sh [ref]` — update the vendored sokol headers from libsk's sokol
  fork (github.com/robknopf/sokol: upstream plus fixes libsk needs; sync the fork
  with floooh/sokol there first). Records the fork and upstream commits in
  `deps/sokol/VERSION`.
- `tools/update_clay.sh [ref]` — the same for Clay (used only by `examples/clay.c`),
  from libsk's fork (github.com/robknopf/clay: upstream plus fixes, each on its own
  branch merged into the fork's `main`). Records both commits in `deps/clay/VERSION`.
- `make loadbench [DESKTOP=1]` — worst frame while loading large glTF models in the
  background vs synchronously (downloads them on first use).
- `make spritebench [DESKTOP=1]` — sprite-heavy scenes with the default sokol_gl
  budgets and with large starting ones: sprites created, frame and CPU time, sokol_gl
  vertex/command use and overflow. The large budgets come from `BENCH_DEFS`, built
  into `build/<target>-bench`.
- `make webcheck [BACKEND=webgpu] [WEB_THREADS=0]` — web build smoke test in a
  browser (needs Emscripten, Node >= 22, a Chromium-based browser; WebGPU runs on a
  virtual X display when Xvfb is installed, else in a visible window). Web builds use
  threads by default, which need cross-origin isolation (`tools/serve.py` sends the
  headers); `WEB_THREADS=0` builds without.
- `python3 tools/serve.py [port] [site] [--tls CERT KEY]` — the dev server (COOP/COEP
  headers, `/assets/` mounted). `--tls` gives other devices on the LAN (a phone) the
  secure page threaded builds need; `localhost` is secure without it.
- `make websize [BACKEND=webgpu] [WEB_THREADS=0]` — wasm/JS sizes per web example
  (raw and gzip; brotli if installed), also summarized after `make wasm-all`.
- Run `make verify` (lib + examples + `make check` + `make test` + `make smoke`,
  about 10 s) before calling a change done; run `make webcheck` (and
  `BACKEND=webgpu`) too when touching rendering, assets or web code.

## Process

- **Ask before changing observable behavior or public API** (`include/*.h`):
  lifecycle, init/run/tick order, what callers may assume. Purely internal
  refactors with no behavioral impact don't need that step.
- For feature work or non-trivial fixes, **outline the plan first** and wait for
  the go-ahead, unless already told to implement.
- **Correct over compatible.** libsk is pre-1.0: when the right design or default
  breaks existing code or examples, choose the right one and update the callers.
  Don't add implicit fallbacks just to keep old behavior working. Still ask before
  changing public API or observable behavior (above), but recommend the correct
  option.
- Read-only tasks (questions, reviews) need no approval.
- **libsk is the primary library; librl is maintenance-only** (see "Direction" in
  README.md). New features go into libsk. Treat librl as a behavior reference and
  parity baseline, not a place to add features.
- **Parity is functional, not 1:1.** Before porting a librl feature, check what it
  does and what went wrong with its design, then propose the libsk design (it may
  be fewer, different, or merged functions). Don't mirror librl signatures by
  default. Record the outcome in `tools/parity.map`.
- **Keep the core a plain C library.** Scripting hosts and language bindings are
  separate modules/repos built on the public API; don't add them here.

## Resource / Object model

libsk layers everything loadable as **Asset → Resource → Object** (full detail in
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
parameter and return value is a **handle (`sk_handle_t`)**, an **integral / float
/ enum**, or a **`const char *`** (paths and text). **No other pointers in user
code** — never `unsigned char *data` / `int size`, never struct pointers. This is
enforced by `tools/check_naming.sh` (run by `make check`), not just convention.

Creation follows one pattern, no exceptions:

- **Resource** ← created from a **path** (or a generator): `sk_texture_create(path)`,
  `sk_mesh_create(path)`, `sk_audio_create(path)`, `sk_font_create(path, size)`,
  `sk_mesh_create_cube(...)`.
- **Object** ← created from a **resource handle**, never a path:
  `sk_sprite3d_create(texture)`, `sk_model_create(mesh)`, `sk_sound_create(audio)`,
  `sk_text2d_create(font, size)`.
- Bare `_create` for both — the **noun** says which (resource noun → path, object
  noun → handle). **No** `_create_from_memory` and **no** "create object from
  file" shortcut; loading bytes and turning them into a resource is internal.
- **Freeing says which layer it is:** resources are reference counted and shared,
  so they have `sk_<resource>_release(handle)` — it drops this handle's reference
  and frees the resource only when the last one goes. Objects are private, so they
  have `sk_<object>_destroy(handle)`. `retain` stays internal: one `create` is one
  reference.

Loading is split from creation (the librl model): the **asset** layer *ensures a
file is local* and fires a **path-only** callback
(`sk_asset_callback_fn(const char *path, void *user)`); the consumer then calls
the sync `sk_*_create(path)`. Bytes never cross into user code.

## Naming

- **Prefix:** all library symbols are `sk_`.
- **Public API** (`include/*.h`): subsystem-first `sk_<section>_<action>`.
- **Cross-`.c` internals** (one `src/*.c` calling another's symbol): `sk_<subsystem>_…`,
  declared **only** in `src/internal/*.h` — not public unless promoted to `include/`.
- **File-local `static`** helpers: no `sk_` prefix; `verb_noun` in `snake_case`;
  shortest name that's unambiguous in the file. Prefer `resolve_*` / `lookup_*` for
  handle→pointer helpers and `is_*` / `has_*` for predicates.
- **Types:** `sk_<noun>_t` — no `_data`/`_instance` suffix. The noun carries the
  layer: resource (`sk_texture_t`, `sk_mesh_t`) vs object (`sk_sprite3d_t`,
  `sk_model_t`). Handle kinds live in `include/sk_handle.h`.
- **Resolved instance pointers:** a local/param holding a raw `sk_<noun>_t *` that
  was resolved from a `sk_handle_t` is named `<noun>_ptr` (e.g.
  `sk_model_t *model_ptr = resolve(handle);`). This keeps the **pointer path**
  visually distinct from the **handle path** at every call site. Don't add `_ptr`
  redundantly where no handle coexists (pure-pointer helpers, value locals).

## Scripted renames

When a `static` helper's old name is a prefix of a longer `sk_*` symbol in the same
file, use **whole-identifier** (word-boundary) replacement, never blind substring
replace, or you'll corrupt the public API. Guard against matching inside comments
(possessives) and reused short names (loop counters, value structs).
