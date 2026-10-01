# Handles: what the type system buys, and what C already does

Written 2026-09-22, because the question "should handles just be `Int`?" kept getting
re-derived from scratch. It applies whatever shape the API layer ends up in, which is
why it lives here rather than beside the flattening trial on the `flat-api` branch.
Everything below was measured, not reasoned about; the commands are given so it can be
re-checked when something changes.

## What a handle is

`wgr_handle_t` is 32 bits, laid out in `src/internal/wgr_handle_pool_internal.h`:

```
  bit 31                        26 25              16 15                    0
  +-------------------------------+------------------+----------------------+
  |          kind (6)             |  generation (10) |      index (16)      |
  +-------------------------------+------------------+----------------------+
```

librl uses the identical scheme — wgrender's `wgr_handle_kind_t` is visibly descended
from `rl_handle_kind_t`, same numbers, with `COLOR = 1` and `MUSIC = 10` retired.

**C checks the kind on every call.** `wgri_handle_pool_resolve` tests it *first*,
before index, occupancy and generation:

```c
if (WGRI_HANDLE_KIND(handle) != (uint8_t)pool->kind) return false;
if (index == 0 || index >= pool->capacity)          return false;
if (!pool->occupied[index])                          return false;
if (pool->generations[index] != generation)          return false;
```

A subsystem's `resolve()` then logs and returns NULL, and the setter returns `false`:

```c
if (handle != 0) log_warn("Invalid model handle (%u)", (unsigned int)handle);
```

Note `handle != 0`: a none handle is silent, deliberately, because `0` is a value you
pass on purpose — you can create a model with mesh `0` and set the real mesh when it
loads. That is librl's convention and wgrender keeps it.

So **the runtime kind check is unconditional and already paid for.** No binding needs
to add one. Passing a `Model` where a `Material` is wanted is caught whatever the
binding's types look like; it comes back as a logged `false` rather than a crash.

## What the type system adds on top

It moves the statically-knowable subset of that check from runtime to compile time. It
does not replace it — see "what types cannot catch" below.

Measured in Haxe 4.3.6 against `src/wgr`, and in Nim 2.2.12 against a `distinct`
sketch of the same design:

| | Haxe typed abstract | Nim `distinct` | one untyped handle | raw `Int` |
| --- | --- | --- | --- | --- |
| `Model` passed as `Material` | rejected | rejected | accepted | accepted |
| bare literal `0` as an argument | rejected | rejected | accepted | accepted |
| `Handle.NONE` as an argument | accepted | accepted (`converter`) | accepted | accepted |
| `m == otherMaterial` | works | needs `{.borrow.}` per level | works | works |
| `m == 0` | **compiles** | rejected | compiles | compiles |
| unassigned field, on js | `undefined` | n/a — zero-initialised | `undefined` | `undefined` |

Two rows deserve their own note.

**`Handle.NONE` passes but a bare `0` does not.** `Material` is declared `from Handle`,
not `from Int`, so `Model.create(Handle.NONE)` compiles and `Model.create(0)` does not.
That is the "0 is deliberate" rule made enforceable instead of documented, and it is the
main thing a single untyped handle gives up. Reproduce:

```
haxe -cp src --main Mix --js /tmp/mix.js   # Material.setRoughness(model, 0.5) -> error
```

**`m == 0` compiles in Haxe and is wrong on js.** The `0` is promoted at the typed
abstract (`Int` -> `Handle` -> `Material`), so an `@:op(A == B)` on `Handle` never sees
the comparison — there is nothing to overload. And an unassigned field on js is
`undefined`, where `undefined == 0` is `false`, so the comparison reports a live handle
for something that holds nothing.

This is **not** caused by the abstracts. Raw `Int` behaves identically:

```haxe
static var asInt:Int;        // never assigned
asInt == 0      // false — the value is `undefined`
asInt == null   // true
```

So dropping the abstracts does not fix it, and all four columns need the same free
`isNone` / `isValid` inline. `wgr.Handle.isNone` carries the `== null` tolerance for
this reason, and any flat replacement has to keep it -- flattening loses the
discoverability of `m.isNone` after a dot, not the correctness, provided the free form
tolerates `null` the same way. (Declaring each kind `@:using` itself, since, puts the
dot back: `m.isNone()` is `Model.isNone(m)`, with nothing to import.)

## What types cannot catch

A **stale** handle — released, its slot reused — has the right kind *and* the right
index. Only the 10-bit generation counter distinguishes it, and no type system sees
that. `wgri_handle_pool_resolve` does.

So the C check and the type check catch different classes of bug and neither subsumes
the other:

| | wrong kind | dead handle | wrong kind, caught early |
| --- | --- | --- | --- |
| C pool resolve | yes | **yes** | no — at the call |
| typed handles | yes | no | **yes** — at compile time |

## Do not add a kind check in the binding

The obvious idea — have the flat API verify the kind before calling — is duplicate work,
and both ways of doing it are worse than not doing it:

- **`wgr_handle_get_kind(h)`** is a real C call per setter. On js that is a marshalling
  round trip, and the frame cost measured at the noise floor
  (0.343 ms against 0.348 ms of script time per frame, measured on the `flat-api`
  branch) would stop being at the noise floor.
- **`(h >>> 26) & 63` inline in Haxe** is genuinely cheap — a couple of instructions —
  but the layout lives in `src/internal/wgr_handle_pool_internal.h`. That is an internal
  header. A binding that reads it breaks silently the day the layout changes, and so
  would every other binding that copied the trick.

C is already doing this, first thing, on every call. Let it.

## Where this leaves the design

Keep the typed abstracts. They cost nothing — zero occurrences in the generated js,
they compile fully away — and they buy two compile-time guards, one of which protects
the `0`-is-deliberate convention. The thing that actually broke cross-binding parity was
**properties**, not typed handles: `tools/audit_binding_parity.py` in librl mirrors C
*names*, and typed handles change no name. Flattening the API buys the parity; dropping
the types buys nothing.

## What the conversion cost, measured

`examples/materials`, the same scene, before and after the whole API flattened:

| | raw | gzipped | script ms/frame |
| --- | --- | --- | --- |
| properties and methods | 20,788 | 4,660 | 0.348 |
| flat statics | 20,846 | 4,665 | 0.339 |

A wash on size (+58 bytes raw, +5 gzipped) and inside the noise on frame cost, which
is the result `inline` predicts: the members compile away either way, so the generated
code is the same calls in the same order.

This corrects the trial, which measured flat as *smaller* (19,975 raw). That number
came from a hand-written slice covering only what one example needed, against the full
sharp API; it was not a like-for-like comparison and the whole conversion does not
reproduce it. Flattening is worth doing for the mapping, the returned `Bool`s and
cross-binding parity — not for bytes.

## What flattening did to the tooling

`tools/setters.py` is gone. It asked "should this member be a property or a method?",
which only has subjects while there are properties; the flat API has one, and it has
no C call behind it. Most of its 388 lines existed to infer *which* C call a member
wrapped, because `wgr_model_set_tint -> Model.tint` cannot be read off a name. Putting
each C call in exactly one member's body deleted the inference, and with it the
`DELEGATED` table of hand-written verdicts and the regex reader of C control flow that
needed it.

What survived moved into `tools/check_refusals.py --check`: a refusal a header names must be
repeated in the binding's doc comment. That half was always the trustworthy one -- prose
against prose, safe to fail a build on, as against the C reading that only warned and had
produced one false clean and two false positives. The member index it needs is
`tools/members.py`, which reads each function's body for its `Raw` calls. It was first
one regex matching a member whose body is one line, which lost members silently when
one grew a second -- `Camera3D.setView`, `Input.getTouch`, anything with a default or a `#if` -- the same
failure the tool it replaced had needed an exception table to paper over.

One exception list remains, `UNREACHABLE`: refusals the binding's types rule out, where
an `enum abstract` with two values cannot produce the third that wgrender would reject.
Documenting those would be noise rather than accuracy.

**What does not become derivable.** `Material.setRoughness` calls
`setFloat(material, "roughness", value)` -> `wgr_material_set_float`. The nine built-in
parameters are many-to-one on `wgr_material_set_float`/`_texture` *in the C*, so no
naming scheme fixes it -- a fixed list written once, not something parsed out of source.

**What belongs in wgrender-c either way.** Eight refusals that log nothing anywhere, and
`wgr_light_set_shadow_map_size` clamping to 256..4096 with no getter to observe the
result. Those are findings about the C library, whatever shape the binding is.

## If wgrender ever gets a Nim binding

`distinct` maps onto the typed abstracts one-for-one, and is strictly stronger on the
two things that bit us:

- `m == 0` is **rejected**, and stays rejected after `{.borrow.}`ing `==`, so the
  `isNone` discipline is enforced rather than documented;
- Nim zero-initialises, so the `undefined == 0` hazard does not exist.

Two Nim-specific costs:

- `NONE`-passes-anywhere needs a `converter` per type, and implicit converters are
  discouraged in Nim style. The alternatives — explicit `Material(0)`, or a generic
  `none[Material]()` — lose the convenience that makes the convention worth having.
- `{.borrow.}` is needed at *each* distinct level. With
  `Material = distinct Handle`, borrowing `==` on `Material` alone fails; `Handle` needs
  it too. Easy to get wrong once, then it is boilerplate.

And Nim's UFCS means the flat statics read as methods for free — `setRoughness(m, 0.5)`
and `m.setRoughness(0.5)` are the same call — so flattening costs nothing in
readability there. Haxe gets the same from `@:using` on each kind, declared once in
the binding rather than at every call site.

For contrast, librl's Nim binding today is `RLHandle* = uint32`, a plain alias: colors
and models share one type and nothing is caught until C sees it. librl's Haxe binding
is `abstract RLHandle(Int) from Int to Int` — one untyped abstract, the third column
above. So `distinct` would be an upgrade on what librl does, not a port of it.
