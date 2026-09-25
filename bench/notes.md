Hand-written, from bench/notes.md; the tables above are generated.

**Reading the call costs.** Only a guest running as JS crosses the boundary per call;
code compiled into the wasm calls wgrender directly. What a crossing adds depends on
what it carries: a scalar call is a couple of ns, a returned struct pays for building a
new object from the heap, and a string pays for copying it in as UTF-8, which is the
dearest by far. So the boundary is worth watching in a call-heavy frame, tens of
thousands of calls, and in string-heavy code (text) first; `simple`'s count above is
nowhere near that, and the stress scene, at one call per entity plus 49 text lines, is
where it can show.

**The pages differ.** C, Nim, Beef and hxcpp are served in wgrender's example shell, a
page with an example picker and a console that also fetches `examples.json`; the Haxe
guest's is `wgr.macros.WebHost`'s minimal page (its `boot.js` counts in the JS column).
Both are counted because both are downloaded; a program shipped in a page of its own
would carry that page's size instead.

**A collector inside the wasm is not measured here.** The GC measurement reads V8's heap, so a
runtime with its own GC in linear memory (hxcpp) shows a clean GC column whether or not
it pauses. The only measurement of one so far is the entity-churn experiment
(`~/projects/beef/wasmtest/churn`: 10,000 entities with heap strings and growing
inventories, Chrome 153, on librl rather than wgrender): hxcpp had the fastest median
tick, 0.055 ms, but a worst of 1.055 ms against 0.34 ms for Beef and Nim ARC, and a p99
of 0.240 against about 0.18. The stress scene now runs hxcpp on a wgrender scene too: its
collections column still shows only V8's, but a pause of hxcpp's own is in the worst
frame and the late frames, with 8 ms of the frame already spent.

**Not measured yet:**

- callbench in Firefox, whose JS -> wasm path is a different engine: every browser
  number here is Chromium's
- hxcpp's collector on a wgrender scene, traced from inside the wasm
- Windows, and the WebGPU backend: every number here is Linux and webgl2
