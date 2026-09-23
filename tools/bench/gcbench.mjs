// Allocation and GC for a built example, in a headless browser.
//
//   node gcbench.mjs --site=DIR [--label=NAME] [--warmup=MS] [--sample=MS]
//                    [--url=PATH] [--probe=FILE] [--uncapped] [--load=MS]
//
// --load burns that many milliseconds of arithmetic in the page's own rAF callback,
// every frame, allocating nothing. It stands in for the game logic this scene does
// not have, and its only purpose is to take the slack away: a collection that fits
// inside a frame with 16 ms spare is invisible, and the same collection in a frame
// with 4 ms spare is a dropped one. Injecting it from here rather than into each app
// keeps it identical across builds written in different languages.
//
// --uncapped takes the vsync limiter off. Note what it does and does not tell you:
// without vsync the rAF rate stops tracking the render rate, because the GPU work is
// queued asynchronously, so the deltas measure how fast the page can issue frames
// rather than how fast it draws them. Useful for pushing allocation, not for timing.
//
// What a collection actually costs comes from V8's own accounting instead: the run is
// traced with devtools.timeline and every MinorGC/MajorGC is reported with its
// duration. Capped at 60 fps with under a millisecond of work in the frame there are
// 16 ms of slack, so those pauses are absorbed whole and the frame times come back
// identical whatever the allocation rate. That is the point being made: the cost is
// real long before anything on screen shows it.
//
// bench.mjs answers what a frame costs. This answers what a frame *allocates*, which
// is the question the frame interval hides hardest: a vsync-capped 60 fps has ~16 ms
// of slack, so a collection has to be enormous before it shows up as a late frame.
// The cost is real long before it is visible, and it is the one cost that scales with
// how long the program has been running rather than with what is on screen.
//
// Samples performance.memory.usedJSHeapSize once per frame, into a preallocated
// Float64Array so the sampler is not itself a source of garbage, and needs
// --enable-precise-memory-info or Chrome rounds the reading to 100 KB. Rising deltas
// are allocation; a fall is a collection. It reports bytes allocated per frame, how
// many collections it saw and how much each returned, and what the frames those
// collections landed on cost.
//
// Only the JS heap: a wasm module's linear memory is not in this number, which is
// exactly the distinction being measured. A C guest should read near zero.
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const arg = (n, d) => process.argv.find((a) => a.startsWith(`--${n}=`))?.split("=").slice(1).join("=") ?? d;
// wgrender's root, two levels up: tools/serve.py and tools/weblib.mjs are its own
const W = resolve(fileURLToPath(import.meta.url), "../../..");
const { findBrowser, freePort, launchBrowser, openSession, RunProcesses, sleep, waitFor } =
    await import("../weblib.mjs");

const site = resolve(arg("site", "out/web"));
const label = arg("label", "gcbench");
const warmup = Number(arg("warmup", 4000));
const sample = Number(arg("sample", 10000));
const probe = arg("probe", "wgrender-host.js");
const url = arg("url", "/");
const uncapped = process.argv.includes("--uncapped");
const load = Number(arg("load", 0));

const run = new RunProcesses(label);
try {
    const port = await freePort();
    run.spawn("python3", [join(W, "tools/serve.py"), String(port), site]);
    await waitFor(`http://127.0.0.1:${port}/${probe}`, "serve.py");
    const { debugBase, browser } = await launchBrowser(run, findBrowser(process.env.WEBCHECK_BROWSER),
        { display: "headless", backend: "webgl2", extraArgs: ["--enable-precise-memory-info",
            ...(uncapped ? ["--disable-gpu-vsync", "--disable-frame-rate-limit"] : [])] });
    const { targetId } = await browser.send("Target.createTarget", { url: "about:blank" });
    const page = await openSession(`ws://${new URL(debugBase).host}/devtools/page/${targetId}`);
    await page.send("Runtime.enable");
    await page.send("Page.enable");
    const gcEvents = [];
    page.onEvent((m) => {
        if (m.method !== "Tracing.dataCollected") return;
        for (const e of m.params.value)
            if (e.name === "MinorGC" || e.name === "MajorGC") gcEvents.push({ name: e.name, us: e.dur ?? 0 });
    });
    await page.send("Page.navigate", { url: `http://127.0.0.1:${port}${url}` });
    // after warmup: the first seconds are asset loads, shader compiles and JIT, and
    // the garbage those make says nothing about the steady state
    await sleep(warmup);
    await page.send("Tracing.start", { traceConfig: { includedCategories: ["devtools.timeline"] } });
    const N = Math.ceil(sample / 16) + 240;
    await page.send("Runtime.evaluate", {
        expression: `globalThis.__n = 0;
            globalThis.__dt = new Float64Array(${N});
            globalThis.__hp = new Float64Array(${N});
            // The burn: 32-bit integer arithmetic, and the iteration count calibrated
            // once so the loop never reads the clock. Both matter -- a float loop boxes
            // its intermediates as heap numbers, and polling performance.now() a few
            // thousand times a frame allocates one per call, either of which would make
            // the harness the biggest allocator in the run and measure itself.
            globalThis.__sink = 1;
            globalThis.__spin = function (n) {
                let x = globalThis.__sink | 0;
                for (let i = 0; i < n; i++) x = (Math.imul(x, 1664525) + 1013904223) | 0;
                globalThis.__sink = x;
            };
            globalThis.__iters = 0;
            if (${load} > 0) {
                globalThis.__spin(5e6); // let it tier up before timing it
                const t0 = performance.now();
                globalThis.__spin(2e7);
                globalThis.__iters = Math.round(2e7 / (performance.now() - t0) * ${load});
            }
            (function tick(p) {
                requestAnimationFrame((t) => {
                    if (globalThis.__iters > 0) globalThis.__spin(globalThis.__iters);
                    if (p && globalThis.__n < ${N}) {
                        globalThis.__dt[globalThis.__n] = t - p;
                        globalThis.__hp[globalThis.__n] = performance.memory.usedJSHeapSize;
                        globalThis.__n++;
                    }
                    tick(t);
                });
            })(0);` });
    await sleep(sample);
    const tracingDone = new Promise((r) => page.onEvent((m) => { if (m.method === "Tracing.tracingComplete") r(); }));
    await page.send("Tracing.end");
    await tracingDone;
    const read = async (e) => JSON.parse(
        (await page.send("Runtime.evaluate", { expression: e, returnByValue: true })).result.value);
    const n = await read("globalThis.__n");
    const dt = await read(`JSON.stringify(Array.from(globalThis.__dt.subarray(0, ${n})))`);
    const hp = await read(`JSON.stringify(Array.from(globalThis.__hp.subarray(0, ${n})))`);
    if (n < 30) { console.error(`${label}: only ${n} frames — did it start?`); process.exit(1); }

    let allocated = 0, freed = 0;
    const collections = [];
    for (let i = 1; i < n; i++) {
        const d = hp[i] - hp[i - 1];
        if (d >= 0) allocated += d;
        else { freed += -d; collections.push({ frame: i, bytes: -d, ms: dt[i] }); }
    }
    const gcUs = gcEvents.map((e) => e.us).sort((a, b) => a - b);
    const gcTotalMs = +(gcUs.reduce((a, b) => a + b, 0) / 1000).toFixed(1);
    // Split them: a median over a handful of collections that are half major says
    // something quite different from a median over three hundred that are nearly all
    // minor, and reporting one number for both invites exactly the wrong reading.
    const byKind = (name) => {
        const us = gcEvents.filter((e) => e.name === name).map((e) => e.us).sort((a, b) => a - b);
        return us.length ? { count: us.length, medianMs: +(us[us.length >> 1] / 1000).toFixed(2),
                             maxMs: +(us[us.length - 1] / 1000).toFixed(2),
                             totalMs: +(us.reduce((a, b) => a + b, 0) / 1000).toFixed(1) }
                         : { count: 0, medianMs: null, maxMs: null, totalMs: 0 };
    };
    const sorted = [...dt].sort((a, b) => a - b);
    const at = (q) => sorted[Math.min(n - 1, Math.floor(n * q))];
    const gcMs = collections.map((c) => c.ms).sort((a, b) => a - b);
    console.log(JSON.stringify({
        label, frames: n, uncapped,
        allocBytesPerFrame: Math.round(allocated / n),
        allocMBPerMinute: +(allocated / n * 3600 / 1048576).toFixed(1),
        collections: collections.length,
        freedMB: +(freed / 1048576).toFixed(2),
        heapStartMB: +(hp[0] / 1048576).toFixed(1),
        heapEndMB: +(hp[n - 1] / 1048576).toFixed(1),
        frameMs: { median: +at(0.5).toFixed(2), p99: +at(0.99).toFixed(2), max: +sorted[n - 1].toFixed(2) },
        gcFrameMs: gcMs.length ? { median: +gcMs[gcMs.length >> 1].toFixed(2),
                                   max: +gcMs[gcMs.length - 1].toFixed(2) } : null,
        // V8's own accounting, which does not care whether the frame had slack to hide in
        loadMs: load,
        lateFrames: { over20: dt.filter((f) => f > 20).length, over33: dt.filter((f) => f > 33).length,
                      pct: +(dt.filter((f) => f > 20).length / n * 100).toFixed(2) },
        gc: { totalMs: gcTotalMs, pctOfWall: +(gcTotalMs / sample * 100).toFixed(2),
              minor: byKind("MinorGC"), major: byKind("MajorGC") },
        bufferFull: n >= N,
    }));
} finally { await run.stop(); }
