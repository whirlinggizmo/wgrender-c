# Plan: colors are values, not handles

Status: **done** (2026-09-17). Implemented as proposed, with the `color_t` ->
`sk_colorf_t` move folded in; see "As built".

## What exists

- `sk_color_create(r, g, b, a)` allocates a slot in a 256-entry pool and returns a
  handle; `sk_color_destroy` frees the slot. 27 built-ins are `extern const
  sk_handle_t` (raylib's palette), handle kind `SK_HANDLE_KIND_COLOR = 1`.
- **Colors are not reference counted and not deduped by value.** librl says why, in
  `src/rl_color.c`: "colors are tiny value objects and do not use refcounted
  shared-asset semantics". libsk inherited the pool unchanged.
- Handle `0` means white (`sk_color_get(0)`), an unresolvable handle draws magenta,
  and `SK_COLOR_DEFAULT` is magenta and unused outside `sk_color.c`.
- 29 public parameters across 13 headers take a color; 12 `src/*.c` files unpack one
  with the internal `sk_color_get`.
- Colors are immutable (no public `sk_color_set`), so animating a tint means
  pre-creating a palette — `examples/sprite2d.c` does exactly that — and with 256
  slots and no dedupe, a color created per frame exhausts the pool in about four
  seconds at 60 fps.

## Goal

A color is a value: nothing to create, destroy, run out of, or resolve. The
handle-only rule in AGENTS.md exists to keep pointers and structs out of the public
API; integers were always allowed, and a color is an integer.

## Design

```c
typedef uint32_t sk_color_t;          /* 0xRRGGBBAA */

#define SK_COLOR_WHITE  0xFFFFFFFFu   /* the 27 built-ins keep their names and values */
#define SK_COLOR_RED    0xE62937FFu
/* ... */

sk_color_t sk_color_rgba(int r, int g, int b, int a);          /* 0..255 */
sk_color_t sk_color_with_alpha(sk_color_t color, int a);       /* fades */
sk_color_t sk_color_lerp(sk_color_t a, sk_color_t b, float t); /* animation */
```

- Every public `sk_handle_t color` / `tint` parameter becomes `sk_color_t`.
- The internal `sk_color_get(handle)` becomes `sk_color_unpack(sk_color_t)`, still
  returning the float struct the renderer already uses. Nothing else in the drawing
  path changes.
- `sk_color_create`, `sk_color_destroy`, the pool and `SK_HANDLE_KIND_COLOR` go away
  (the kind is retired with a comment, like kind 10 for music).
- The helpers are real exported functions, not macros, so bindings and the wasm
  build get them for free.

### Two color types, and which is which

There are two today, and they stay two — the renderer works in floats (`sgl_c4f`
takes them, and the sRGB -> linear conversion needs them), so the float struct does
**not** become a `uint32`:

| | today | proposed |
|---|---|---|
| in the public API | `sk_handle_t` (a pool handle) | `sk_color_t` = `uint32_t`, packed `0xRRGGBBAA` |
| inside the renderer | `color_t` — `{float r, g, b, a}` — via `sk_color_get(handle)` | `sk_colorf_t`, same struct, via `sk_color_unpack(sk_color_t)` |

`color_t` is declared in the **public** `include/sk_types.h` today, but no public
function uses it (only 12 `src/*.c` files do, through `sk_color_get`), and it is the
one type in the repo with no `sk_` prefix although it crosses `.c` files, which
AGENTS.md requires. So it moves to `src/internal/sk_color.h` next to
`sk_color_unpack` and becomes `sk_colorf_t` — one struct fewer on the surface every
binding generator has to read, and `sk_color_t` (packed, public) vs `sk_colorf_t`
(float, internal) can't be misread for each other the way `sk_color_t` vs `color_t`
could.

### The one behavior change: `0` stops meaning white

Today every color parameter treats handle `0` as white. Packed, `0x00000000` is
transparent black — which is `SK_COLOR_BLANK`, a real value in the palette — so `0`
cannot keep meaning white without stealing it.

**Colors become explicit: pass `SK_COLOR_WHITE`.** White is the identity for a tint,
so the internal `tint != 0 ? tint : white` branches collapse instead of growing, and
"no tint" and "white tint" stop being two spellings of one thing. `examples/ui.c` and
`examples/2d.c` pass `0` for "no tint" through ternaries; they become
`SK_COLOR_WHITE`. This follows "correct over compatible" in AGENTS.md: no implicit
fallback kept alive just to avoid touching callers.

### Migration risk

`sk_color_t` and `sk_handle_t` are both 32-bit unsigned, so a stale color *handle*
passed to a color parameter still compiles and silently reads as packed RGBA. Inside
the repo nothing survives the change — `sk_color_create` is gone, so every creation
site is a compile error — and nothing depends on libsk yet. Worth stating in the
commit message.

## Cost

29 signatures in 13 public headers, 12 `src/*.c` files, roughly 180 call sites in
examples and tests, plus docs and `tools/parity.map`. Comparable to the shape split
(31 files), mostly mechanical, one pass with the full verify rig behind it. It
removes a pool, a handle kind, two public functions, the 256-color ceiling and the
"which colors do I own?" question.

## Decisions

1. **Packing `0xRRGGBBAA`** — hex literals read the way they look (`0xFF0000FF` is
   opaque red), and alpha last matches how the built-ins are written. Recommend: yes.
2. **Explicit colors, no `0` sentinel;** `SK_COLOR_WHITE` is the identity tint.
   Recommend: yes.
3. **Helpers:** `sk_color_rgba`, `sk_color_with_alpha`, `sk_color_lerp`. The last two
   are the cases that motivated this (fading and animating a tint without a palette).
   Recommend: yes, all three.
4. **Drop `SK_COLOR_DEFAULT`** (magenta, unused); `SK_COLOR_MAGENTA` already exists.
   Recommend: yes.
5. **Keep the built-in palette names and values** (raylib's): familiar, and free as
   `#define`s. Recommend: yes.

## As built

- `sk_color_t` (packed `0xRRGGBBAA`) in `sk_types.h`; the 26 built-ins are `#define`s
  with their raylib values in `sk_color.h`; `sk_color_rgba`, `sk_color_with_alpha` and
  `sk_color_lerp` are exported functions. `SK_COLOR_DEFAULT` (magenta, unused) is gone.
- The float struct moved to `src/internal/sk_color.h` as `sk_colorf_t`, with
  `sk_color_unpack`. `src/sk_color.c` went from a 190-line pool to 50 lines of
  arithmetic; `sk_color_init` / `sk_color_deinit` and their calls in `sk.c` are gone,
  and handle kind 1 is retired in `sk_handle.h`.
- **Defaults had to move with the meaning of 0.** Every object that defaulted its
  tint or color to handle 0 (shape2d, shape3d, sprite2d, sprite3d, model, text2d,
  text3d, light, scene ambient) now defaults to `SK_COLOR_WHITE`, because 0 is
  transparent black. The compiler cannot catch this — `sk_color_t` and `sk_handle_t`
  are both 32-bit unsigned — so it was done by reading every initializer, and the
  examples (which draw every kind) are the check.
- Building and reading components: `sk_color_rgba` (0..255) and `sk_color_rgbaf`
  (0..1, rounded to the nearest step), both clamping — out-of-range components
  saturate and never wrap into the neighbouring channel — plus `sk_color_get_red`,
  `_green`, `_blue`, `_alpha`.
- **No constant-expression macro.** It was considered so a palette could be declared
  at file scope (`sk_color_rgba(...)` is a function call, so it cannot initialize
  anything with static storage duration). Dropped, because a macro can't clamp
  without evaluating its arguments more than once, and clamping everywhere matters
  more than that convenience. The built-ins carry their components as comments
  instead, and the test below checks the literal against the function, so the two
  stay independent rather than one deriving from the other.
- `tests/unit/color_test.c` covers all 26 built-ins against their documented
  components, packing and clamping for both constructors, `with_alpha`, `lerp`
  endpoints and clamping, the component getters, unpacking, and round trips.
- **What the `0` change actually broke, and how it was caught.** Seven examples called
  `sk_scene_set_ambient(scene, 0, ...)` — handle 0 meaning white — which silently
  became transparent black, so their 3D models lost all ambient light. The compiler
  can't see it (`sk_color_t` and `sk_handle_t` are the same width), the unit tests
  don't render, and `make smoke` only checks that frames run without errors. It showed
  up in the webcheck screenshot of `ui`, as a gumshoe several shades too dark.
  Afterwards every example's screenshot was compared with its pre-change version by
  mean brightness: all 22 matched within 0.2 (backend noise), which is the check that
  actually covers this class of change.
- Sizes barely move, as expected for removing a small pool: `hello` 323.3 -> 322.9 KB
  gzip, 265.4 -> 264.6 KB brotli.

## Verification

`make verify`, ASan, `make parity` (librl's `rl_color_create` / `rl_color_destroy`
need map entries: created values, no lifecycle), webcheck on both backends, and
`make websize` before and after — the pool and its handle plumbing should come out of
the wasm. Unit tests: packing round-trips through `sk_color_rgba` / `sk_color_unpack`,
`with_alpha` and `lerp` endpoints, and a drawing path that takes a literal color.
