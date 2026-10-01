Hand-written, from bench/notes.md; the tables above are generated.

**Why the JS guest allocates.** The binding's calls compile away, but a call that
returns a struct builds a Haxe value object from the heap every time: `Vec2`,
`MouseState`, `PickResult`, the `Text.measureEx` result (the calls table lists which
`simple` makes). That is the guest's JS heap traffic above the page's noise floor; at 60
fps its collections are absorbed in the frame's slack.

**Why the guest's host wasm is smaller than the C.** `wgr.macros.WebHost` links the
host exporting only the wgrender calls the compiled guest makes, so wgrender code the
example never reaches is stripped. The C build exports nothing but still links what
its own `simple.c` reaches, so the host comes out no larger than the C.

**hxcpp's GC is not in the GC table.** It runs in the wasm's linear memory, where
gcbench cannot see it, so the hxcpp row reads clean whether or not it pauses. See
wgrender's `docs/benchmarks.md` notes for the one measurement of it (the entity-churn
experiment: a worst tick of about 3x Beef's and Nim's).

**What the stress scene allocates.** The entities that die and are replaced, and the
text built every frame: what any Haxe game would allocate. The transforms themselves
allocate nothing. `setTransform` takes every part (none optional), so a `new Vec3(...)`
handed to it compiles away; when rotation and scale were optional, the null check on
them stopped that, and each call allocated two `Vec3`s, most of the guest's garbage.
