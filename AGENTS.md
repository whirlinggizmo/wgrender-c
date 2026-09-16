# AGENTS

Conventions for working in **libsk** (a sokol-based C library, evolved from librl).
Keep this file short and rule-shaped. The authoritative design doc is
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build & verify

- `make` — build the static library (`lib/libsk.a`).
- `make examples` — build everything in `examples/`.
- `make check` — guardrails; currently enforces that `include/` and `examples/`
  stay **backend-free** (no sokol/GL leakage into the public surface).
- Build clean (lib + examples + `make check`) before calling a change done.

## Process

- **Ask before changing observable behavior or public API** (`include/*.h`):
  lifecycle, init/run/tick order, what callers may assume. Purely internal
  refactors with no behavioral impact don't need that step.
- For feature work or non-trivial fixes, **outline the plan first** and wait for
  the go-ahead, unless already told to implement.
- Read-only tasks (questions, reviews) need no approval.
- **libsk is the primary library; librl is maintenance-only** (see "Direction" in
  README.md). New features go into libsk. Treat librl as a behavior reference and
  parity baseline, not a place to add features.
- **Parity is functional, not 1:1.** Before porting a librl feature, check what it
  does and what went wrong with its design, then propose the libsk design (it may
  be fewer, different, or merged functions). Don't mirror librl signatures by
  default. Record the outcome in `tools/parity.map`.

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
