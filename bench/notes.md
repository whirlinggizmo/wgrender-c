Hand-written, from bench/notes.md; the tables above are generated.

**What a JS guest's calls cost a frame.** Multiply the call rates by the count. `simple`
makes 16 wgr calls a frame from its Haxe guest: six carry a string (five
`wgr_text_draw_ex`, one `wgr_text_measure_ex`), four return a struct (mouse state,
screen size, pick, the measure), and the rest are scalar. At the boundary rates above
that is about 0.2 µs a frame, against a 16.7 ms budget. For scale, 1 ms of script buys
roughly 380k scalar calls, 110k struct returns or 32k string calls, so the boundary starts
to matter at tens of thousands of calls a frame, and strings are where to look first.

**A collector inside the wasm is not measured here.** gcbench reads V8's heap, so a
runtime with its own GC in linear memory (hxcpp) shows a clean GC column whether or not
it pauses. The only measurement of one so far is the entity-churn experiment
(`~/projects/beef/wasmtest/churn`: 10,000 entities with heap strings and growing
inventories, Chrome 153, on librl rather than wgrender): hxcpp had the fastest median
tick, 0.055 ms, but a worst of 1.055 ms against 0.34 ms for Beef and Nim ARC, and a p99
of 0.240 against about 0.18.

**Not measured yet:**

- a call-heavy scene: thousands of transforms or text draws a frame, where the rates
  above would show
- callbench in the browser, not only Node, and in Firefox, whose JS -> wasm path is a
  different engine
- hxcpp's collector on a wgrender scene, traced from inside the wasm
- gcbench with `--load`, which takes the vsync slack away so a pause that would cost a
  frame shows as a late one
- Windows, and the WebGPU backend: every number here is Linux and webgl2
