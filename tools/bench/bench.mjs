// Frame time for a built example, in a headless browser.
//
//   node bench.mjs --site=DIR [--label=NAME] [--warmup=MS] [--sample=MS]
//                   [--url=PATH] [--probe=FILE]
//
// --url picks a page within the site, for wgrender's own example shell, which
// serves every example from one index and selects with ?ex=NAME.
//
// Samples requestAnimationFrame deltas in the page and prints the median and the
// 95th percentile in milliseconds, plus how many frames it saw. The point is
// comparing the same example built two ways -- wgrender's C against the Haxe guest --
// so what matters is the difference, not the absolute number: a headless GPU is not
// a real one, and both sides are equally disadvantaged by it.
//
// rAF is capped at the display rate, so a fast example reads ~16.7 ms on both sides
// and says nothing about either. What separates them is the work inside the frame, so
// this also takes Chrome's own CPU accounting (Performance.getMetrics) across the
// window and reports script and total task time per frame.
//
// ScriptDuration is the interesting one here: in the C build almost nothing runs in
// JS, while the Haxe guest's whole game loop does, plus a marshalling step per
// wgrender call. That difference is the price of this architecture, and it is what
// the frame interval hides.
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const arg = (n, d) => process.argv.find((a) => a.startsWith(`--${n}=`))?.split("=").slice(1).join("=") ?? d;
// wgrender's root, two levels up: tools/serve.py and tools/weblib.mjs are its own
const W = resolve(fileURLToPath(import.meta.url), "../../..");
const { findBrowser, freePort, launchBrowser, openSession, RunProcesses, sleep, waitFor } =
    await import("../weblib.mjs");

const site = resolve(arg("site", "out/web"));
const label = arg("label", "bench");
const warmup = Number(arg("warmup", 4000));
const sample = Number(arg("sample", 8000));
const probe = arg("probe", "wgrender-host.js");
const url = arg("url", "/");

const run = new RunProcesses(label);
try {
    const port = await freePort();
    run.spawn("python3", [join(W, "tools/serve.py"), String(port), site]);
    await waitFor(`http://127.0.0.1:${port}/${probe}`, "serve.py");
    const { debugBase, browser } = await launchBrowser(run, findBrowser(process.env.WEBCHECK_BROWSER),
                                                       { display: "headless", backend: "webgl2" });
    const { targetId } = await browser.send("Target.createTarget", { url: "about:blank" });
    const page = await openSession(`ws://${new URL(debugBase).host}/devtools/page/${targetId}`);
    await page.send("Runtime.enable");
    await page.send("Page.enable");
    await page.send("Page.navigate", { url: `http://127.0.0.1:${port}${url}` });
    await page.send("Performance.enable");
    await sleep(warmup);
    const metrics = async () => Object.fromEntries(
        (await page.send("Performance.getMetrics")).metrics.map((m) => [m.name, m.value]));
    const before = await metrics();
    // start sampling only after warmup: the first seconds are asset loads, shader
    // compiles and JIT, none of which is the steady-state frame cost being compared
    await page.send("Runtime.evaluate", {
        expression: `globalThis.__f = []; (function tick(p) {
            requestAnimationFrame((t) => { if (p) globalThis.__f.push(t - p); tick(t); });
        })(0);` });
    await sleep(sample);
    const after = await metrics();
    const { result } = await page.send("Runtime.evaluate", {
        expression: "JSON.stringify(globalThis.__f)", returnByValue: true });
    const frames = JSON.parse(result.value).sort((a, b) => a - b);
    if (frames.length < 30) {
        console.error(`${label}: only ${frames.length} frames — did it start?`);
        process.exit(1);
    }
    const at = (q) => frames[Math.min(frames.length - 1, Math.floor(frames.length * q))];
    const missed = frames.filter((f) => f > 18).length / frames.length;
    const per = (name) => +(((after[name] ?? 0) - (before[name] ?? 0)) * 1000 / frames.length).toFixed(3);
    console.log(JSON.stringify({
        label, frames: frames.length,
        median: +at(0.5).toFixed(2), p95: +at(0.95).toFixed(2),
        overCap: +(missed * 100).toFixed(1),
        scriptMs: per("ScriptDuration"), taskMs: per("TaskDuration"),
    }));
} finally { await run.stop(); }
