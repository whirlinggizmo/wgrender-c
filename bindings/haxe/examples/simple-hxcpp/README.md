# simple-hxcpp — wgrender in Haxe, compiled into the wasm

A port of wgrender's `examples/simple.c` to Haxe, alongside the same example in
[Nim](../../nim/simple) and [Beef](../../beef/simple). The binding itself is the
[wgrender-hx](../..) haxelib, shared with
[../simple](../simple), which runs the same scene as a guest module instead. Same scene in all four: an
animated model, a bobbing 3D sprite, looping music, two TTF fonts, a centred message
that reports what the mouse is over (scene picking), and a debug overlay with timers,
mouse state and the platform name.

It exists to answer two questions: what do idiomatic wgrender bindings look like in
Haxe, and what does Haxe cost in wasm.

## Build

From `bindings/haxe/`, as every example is built (`guestbuild.HxcppProject`):

```sh
tools/run_examples.py desktop simple-hxcpp   # out/linux-x64/release/bin/simple (out/windows-x64-msvc/release/bin/ on Windows)
tools/run_examples.py web simple-hxcpp       # out/wasm32/release-hxcpp/site/ (simple.js + simple.wasm + the library's page)
tools/run_examples.py all simple-hxcpp
tools/run_examples.py drive simple-hxcpp     # headless-browser smoke test of the web build; writes build/wasm32-release-hxcpp/check.png
tools/run_examples.py compare simple-hxcpp   # this port's wasm next to the C one
tools/run_examples.py clean simple-hxcpp
```

Serve the web build with wgrender's `tools/serve_site.py <port> examples/simple-hxcpp/out/wasm32/release-hxcpp/site`.

That runs `haxe build.hxml` or `haxe web.hxml`, and hxcpp compiles wgrender in
from its sources, as every build of the binding does (the binding's
`project/Build.xml`), with whichever compiler it uses: MSVC or MinGW on
Windows, emcc for the web. The C++ and objects are in `build/<preset>/cpp`
(`build/linux-x64-release`, `build/wasm32-release-hxcpp`), where `wgr.macros.NativeOut`
puts them. `haxe build.hxml` works on its own too.

Web options are wgrender's own web build settings, read from the environment
(`BACKEND=webgl2|webgpu`, `WEB_DEBUG=0|1`). Web builds are always `WEB_THREADS=0`: hxcpp's emscripten target is
single-threaded.

Needs Haxe 4.3, hxcpp, a C compiler, and for the web Emscripten (`emcc` on the path).

## The route to wasm

Haxe has no wasm backend. The comparable path — and the one this uses — is the same
AOT route Nim and Beef take: **Haxe → hxcpp (C++) → Emscripten → wasm**, driven by
hxcpp's `emscripten` toolchain (`-D emscripten`). Haxe's other web target — plain JS
calling wgrender's Emscripten module through JS externs — is a different shape of
binding and is not what this measures, but it is the cheaper way to ship: see "The
other route" below.

## Size

`tools/run_examples.py compare`, all built with the same wgrender web flags
(`BACKEND=webgl2 WEB_THREADS=0`, release):

| port                   |       wasm |   gzipped |  vs C |        js |  gzipped |
|------------------------|-----------:|----------:|------:|----------:|---------:|
| C (wgrender's example) |    695,714 |   293,621 | 1.00x |    65,959 |   23,382 |
| Nim                    |    720,009 |   306,801 | 1.03x |    54,980 |   20,771 |
| Beef                   |    719,533 |   304,723 | 1.03x |    67,595 |   24,111 |
| Haxe (this)            |  1,706,364 |   469,575 | 2.45x |    70,894 |   25,307 |

Roughly a megabyte of wasm over C, 176 KB of it after gzip (measured with hxcpp from
robknopf/hxcpp; the 4.3.2 release makes 1,072,316 bytes, without the GC pass below). Almost all of it is the
hxcpp runtime rather than anything this example does: a Haxe hello-world through the
same toolchain is already 677 KB of wasm on its own — hxcpp ships a mark/sweep GC
(with binaryen's `--spill-pointers` so it can scan the wasm stack), reference-counted
UTF-16-aware strings, `Dynamic`, and class reflection tables, and `-dce full` only
removes what it can prove unreachable. Nim's ARC and Beef's arena/ownership model have
far less to carry into the binary.

The JS glue is the same size everywhere, because it's almost entirely Emscripten's.

Two caveats on the table. `tools/run_examples.py compare` reads whatever each sibling project
last built, so the Nim row shows 726,858 / 307,166 unless it was built with
`WEB_THREADS=0` (its default is threads); the number above is the non-threaded build,
so it lines up with the rest. And the Haxe link needs one extra flag that the others
don't — see "Two things that needed working around" below.

## The other route: Haxe → JS

There is a second way to put Haxe on the web, and it avoids most of the cost above:
compile Haxe to **JS** and leave wgrender a separate Emscripten module that the JS
calls from the outside, rather than compiling Haxe *into* the wasm. The wasm then
contains only wgrender — the C row, ~690 KB — and the Haxe side is a JS file. It is
not what the table above measures, because it doesn't put Haxe in the wasm at all;
it's a different architecture, not a different flag.

The Haxe side costs almost nothing. The same small program (a `Map`, formatted
output, `Math.sin`) through each language's JS backend at release settings:

| backend                     |    bytes | gzipped |
|-----------------------------|---------:|--------:|
| Haxe → JS (`-dce full`)     |      951 |     480 |
| Nim → JS (`-d:release`)     |   49,312 |   9,100 |

Note the inversion: Haxe maps onto native JS types and carries essentially no runtime,
where Nim's JS backend brings a real one — the opposite of the wasm ranking, where Nim
is 1.03x C and Haxe is 2.43x. Both are noise against 690 KB of wasm, so it doesn't
change the outcome, but it's worth knowing which way round it goes.

### What the boundary actually costs

The obvious worry is JS↔wasm crossings. Measured (emcc 5.0.7 `-O3`, node 22, 5M
iterations warmed, on the shapes wgrender's API actually uses):

| crossing                                            | ns/call |
|-----------------------------------------------------|--------:|
| plain call, handle + 3 floats                        |     3.5 |
| plain call, handle + 9 floats (`set_transform`)      |     3.1 |
| `vec2_t` return via sret + heap read                 |     2.4 |
| `wgr_pick_result_t` return + 8-field heap read       |     4.6 |
| **JS string → UTF-8 on the wasm stack → call**       | **104.5** |

The crossing itself is ~3 ns and barely notices arity. Strings are 30x that, and are
the only shape that costs anything.

This example's frame path is 15 wasm calls — 6 plain, 3 returning structs, 6 carrying
text — so about **660 ns per frame, 0.004% of a 16.6 ms budget**. It would take
~47,000 plain calls per frame, or ~1,600 string calls, to lose 1% of frame time.

Two properties of wgrender make that hold, and both are deliberate (see its docs/CONVENTIONS.md).
The scene is retained-mode, so `wgr_scene_draw` is one call whether it draws 3 objects
or 30,000 — you pay per object you *mutate* each frame, not per object drawn; even
`spritebench`'s shape, 5,000 sprites each re-transformed every frame, is ~17 µs, about
0.1% of a frame. And the rule that every parameter is a handle, a scalar or a
`const char*` means there is nothing to marshal on the non-string calls, which is why
they are 3 ns.

Text is the one shape that costs anything — and wgrender already answers it. This
example re-sends six strings a frame because it uses the *immediate* path,
`wgr_text_draw_ex`, which it inherits from the C example it ports. The retained path
is `Text2D` / `Text3D`: handle objects created from a `Font`, given their string once
with `wgr_text2d_set_text` (which copies it), and added to a scene like any other
drawable, so `wgr_scene_draw` draws them. A string then crosses the boundary when it
*changes*, not once per frame — which for a typical UI, where most text is static, is
close to never. (It retains the state, not the geometry: the header is explicit that
text is still shaped on every draw, at the same per-frame cost as the immediate path.
That doesn't matter here — what matters is that the string stops crossing.)

This example is close to the worst case for its own content either way: four of its
six strings (the two timers, the mouse readout, the pick message) genuinely change
every frame, so retained text would save it maybe two crossings out of six. At 0.004%
of a frame, none of it signifies — the point is that the ceiling is set by how often
text *changes*, not by how much of it you draw.

(Only the JS→wasm direction was measured. The other way, `addFunction` for the frame
and asset callbacks, is once a frame plus a handful at startup.)

### It was built — see ../simple

The estimates below turned out close, so the section now reports results instead.
[../simple](../simple) is the same scene with wgrender as a wasm host and this
example's code compiled to JS as a guest module, verified in a browser down to the
pick message.

| port | wasm | js | total | gzipped | vs C |
|---|---:|---:|---:|---:|---:|
| C (wgrender's example) |   695,714 | 65,959 |   761,673 | 317,003 | 1.00x |
| Nim |   720,009 | 54,980 |   774,989 | 327,572 | 1.02x |
| Beef |   719,533 | 67,595 |   787,128 | 328,834 | 1.03x |
| Haxe, in the wasm (this) | 1,706,364 | 70,894 | 1,777,258 | 494,882 | 2.33x |
| Haxe, as a guest (../simple) |   694,791 | 90,156 |   784,947 | 321,396 | **1.03x** |

The guest route's 90,156 of JS is 67,477 of Emscripten glue, **22,213 for the whole
game**, and a 466-byte boot shim. So Haxe costs about 3% over writing it in C, rather
than 2.3x, once its runtime stops being compiled into the wasm.

Two things the estimates got right and one they missed:

- **The binding was reusable.** `wgr/Wgr.hx` needed 37 guard lines and 27 real edits
  to compile for both — 3.9% of the file, against the 39-of-1,119 guessed here.
- **The five struct returns** do come back through a hidden out-pointer; confirmed
  against the wasm C ABI before the JS layer trusted it.
- **Missed:** that the host ABI would be nearly free. `wgr_guest.c` plus the 52 calls
  the guest makes, exported, is 923 bytes *less* wasm than wgrender's own C `simple`.

The half that motivated it holds too: `../simple` builds the same source to JS for the
web and through hxcpp for the desktop, so one source serves both.

## The bindings

The binding lives in the wgrender-hx haxelib; this project is the example and its
build. There, `src/wgr/` is one module per wgrender public header, over a flat per-target C surface
— the shape librl's Haxe bindings use (`rl/Model.hx`, `rl/Asset.hx`, … over one
`rl/impl/RLImpl.<target>.hx`). Files, not directories: `wgr/Model.hx`, not `wgr/Model/`.
The C surface lives under `wgr/impl/`, as librl's does, so `import wgr.*;` gives a
consumer the API and none of the plumbing. `wgr/import.hx` gives every public module
the C surface without each one repeating it.

Two layers, the same split the Nim port uses:

- **`wgr/Raw.hx`** — the slice of the C API this example uses, as is: C names, C types,
  declared with `@:include("wgr.h")` so the C++ compiler checks every prototype and
  struct layout. A wrong argument type or a stale struct field is a compile error, not
  a crash. Not bound: 2D sprites and shapes, particles, materials, custom shaders,
  the environment, events and gamepads.
- **`wgr/Wgr.hx`** — the layer you actually write against.

The design goal was that the whole wrapper compile away. Every handle type is an
`abstract` over `Int`, every member is `inline`, so a call costs exactly what the C
call costs; the only allocations are the small value objects (`Vec2`, `Vec3`,
`MouseState`, `PickResult`) the wrappers hand back.

The API is flat: every operation is a static named after the C call it makes, taking
the handle first. The typed abstracts stay, so the shape is C's and the types are
Haxe's:

```haxe
// one abstract per handle kind — a Mesh where a Texture belongs is a compile error,
// and so is a bare literal 0 where Handle.NONE is accepted
final mesh = new Mesh(CHARACTER_PATH); // loads on create; Pending until it has
model = new Model(mesh);      // wgrender's rule: object from resource, resource from path
mesh.release();               // the model holds its own reference
model.setAnimationLoop(true);
model.setTint(Color.RAYWHITE);
model.setPosition(0, 0, 0);
scene.add(model);             // takes a Model, Sprite3D or Light, and nothing else

// nothing called back: a task is read in a frame, then destroyed
final task = Asset.ensure(path);
if (task.getStatus() == Done) open(task.getPath());

// the pick result's untyped handle still compares against typed ones
if (pick.handle == model) ...
```

Why flat rather than methods and properties: each C call is then exactly one member,
so the binding can be audited mechanically and a second binding in Lua or Nim covers
the same calls one for one — and a property setter cannot return the `Bool` a wgrender setter
uses to refuse, which was silently discarded in nine places. `docs/handles.md` has the
measurements; it cost nothing, either in bytes or per frame.

Two places where the Haxe types say more than the C ones:

- **Split align.** wgrender has one `wgr_text_align_t` covering both axes
  (LEFT/CENTER/RIGHT, TOP/MIDDLE/BOTTOM) and returns `false` if you pass a value meant
  for the other one. Here it's two enums, `AlignX` and `AlignY`, both casting to the
  same C type — so `setAlign(Top, Left)` is a compile error rather than a silent
  runtime `false`.
- **`destroy` vs `release`.** wgrender's split is that objects are private and
  destroyed, resources are shared, reference-counted and released. Every object
  abstract has `destroy()` and every resource has `release()`, and nothing has both,
  so the layer a handle belongs to is visible at the call site.

Enums are `enum abstract`s over `Int`; window and asset flags are or-able
(`Msaa4x | Resizable`). The `Key` table is generated from wgrender's `wgr_keys.h` by
`tools/gen_keys.py`, and carries `static_assert`s that fail the C++ build if the
header's numbers ever move — the same guard the Nim port uses.

`test/CheckBindings.hx` (`tools/check_binding.py`) **runs** against headless wgrender — no
window, GPU or audio, with `WGR_HEADLESS_FRAMES` driving a few frames — and asserts
what it gets back. 317 checks: struct field order, colour packing, enum values against
the headers, accessor round-trips, and behaviours the headers state exactly (a missing
asset yields a none handle; an unknown material parameter is refused; a point light
refuses shadows where a directional one takes them; an emitter with the default rate
of 0 makes nothing until it bursts).

It compiled-but-never-ran before, which proves the API exists but not that it reaches
the right C call — a grouped wrapper with two arguments transposed compiles perfectly.
Transposing `Text.toVec2`'s x and y now fails two checks and exits non-zero, which is
the point.

It is the longest of the three bindings, at 1,119 hand-written lines against Nim's 342
and Beef's 187 (each excluding its generated key table) — though it also covers more:
the retained text objects, which the other two skip. Beef is short because it stops at
the raw C surface; Nim and this one both wrap it, and Nim does the same job in far
fewer lines. Most of the gap is per-handle boilerplate: Nim gets a typed handle from
one `distinct Handle` line and shares `isNone` and `==` across all of them with a
typeclass, where each Haxe `abstract` has to restate `isNone` and its conversion to the
raw C type, and name every accessor separately.

### Compared with the older librl Haxe binding

`librl/bindings/haxe` is a flat façade — `Model.setTransform(handle, 9 floats)` — with
a single untyped `RLHandle` for everything. This binding is now flat the same way, and
for librl's reason: four bindings only stay in step if each C call is one name in all
four. Where it differs is the handle. librl passes one untyped `RLHandle`
everywhere, and this keeps a distinct type per kind, which costs nothing at runtime and
rejects both a `Mesh` where a `Texture` belongs and a bare literal `0` where
`Handle.NONE` is meant. So: librl's shape, Nim's types. `docs/handles.md` has the
measurements behind both halves.

## Exceptions have to be caught at the callback edge

wgrender owns the loop (`sapp_run`), so your code runs as its callbacks. An exception
that escapes one of those unwinds into C frames that can't handle it, and the report
gets worse the further it goes. Measured, with a callback that throws on frame 30:

| build          | what happens                                                              |
|----------------|---------------------------------------------------------------------------|
| hxcpp native   | message printed, unwinds out of `main`, exit 255                          |
| hxcpp web      | **page freezes; `Uncaught [object WebAssembly.Exception]`, message gone**  |
| via JS externs | page freezes, but with a readable JS error                                |

Frozen means frozen: the frame counter stopped at 30 and was still 30 four seconds
later. So `wgr.Wgr` catches in every trampoline — init, frame, and the asset
callbacks — and logs through wgrender's logger instead. A bad frame stays a bad
frame, and the message survives:

```
[ERROR] uncaught exception in the frame callback: boom from the Haxe frame callback
```

It logs `e.message`, not `e.details()`: hxcpp has no stack to add unless the build
defines `HXCPP_STACK_TRACE`, so `details()` says the same thing for 57 KB more wasm.
The guard as it stands costs 11 KB (4.5 KB gzipped), which is in the table above.

Anything driving wgrender from JS externs instead would need the same guard, for the
same reason — it's a property of sokol_app owning the loop, not of hxcpp.

## Two things that needed working around

**C++ is stricter than C about enums.** wgrender's headers take real enum types
(`wgr_keycode_t`, `wgr_light_type_t`, ...), and C++ won't take an `int` there — which
C, and so the Nim port, will. Each Haxe enum carries a `@:to` that emits the cast, so
the call sites stay clean (`wgr.Wgr`, search for `toRaw`).

**The web link needs the wasm stack ops exported.** wgrender's `EM_JS` glue makes
Emscripten's JS `$stackSave`/`$stackRestore` live, but under hxcpp's
`-fwasm-exceptions` nothing native references the wasm stack operations, so emcc
leaves them out of the link and it fails on `emscripten_stack_get_current` /
`_emscripten_stack_restore`. `build.py` adds
`-sEXPORTED_FUNCTIONS=_main,_emscripten_stack_get_current,__emscripten_stack_restore`,
which pulls them back in. The C, Nim and Beef builds don't hit this: without wasm
exceptions the legacy exception glue references those symbols natively.

## Layout

```
build.hxml  web.hxml               the builds
build.py                           builds wgrender, then haxe; serve / compare / clean
src/Simple.hx                      the example
src/Defines.hx                     reads a -D name=value define's value (needs a macro)
src/wgr/Wgr.hx                     the binding
src/wgr/impl/Raw.hx                the C API as is
test/CheckBindings.hx              compile-check of the whole binding surface
tools/gen_keys.py                  regenerates the Key table from wgrender's wgr_keys.h
```
