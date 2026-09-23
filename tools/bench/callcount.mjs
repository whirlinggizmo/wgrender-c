// wgrender calls per frame made by a JS guest, in a headless browser.
//
//   node callcount.mjs --site=DIR [--label=NAME] [--warmup=MS] [--sample=MS]
//                      [--url=PATH] [--probe=FILE] [--guest=NAME]
//
// Only a guest that runs as JavaScript crosses into the wasm to call wgrender, so
// only it has calls to count; a C, Nim or hxcpp program calls wgrender inside the
// wasm and never touches JS to do it. The count is what turns callbench's per-call
// rates into a per-frame cost.
//
// The guest is handed the host by `<guest>.start(host)` (the guest ABI's boot, see
// wgr_guest.h). Before the page loads, this traps the global the guest defines and
// wraps its start, so every `_wgr_*` export on the host it is given counts its calls.
// Nothing in the built site changes. The wrapper adds a function call to each wgr
// call, so run bench.mjs, not this, for frame time.
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const arg = (n, d) => process.argv.find((a) => a.startsWith(`--${n}=`))?.split("=").slice(1).join("=") ?? d;
// wgrender's root, two levels up: tools/serve.py and tools/weblib.mjs are its own
const W = resolve(fileURLToPath(import.meta.url), "../../..");
const { findBrowser, freePort, launchBrowser, openSession, RunProcesses, sleep, waitFor } =
    await import("../weblib.mjs");

const site = resolve(arg("site", "out/web"));
const label = arg("label", "callcount");
const warmup = Number(arg("warmup", 4000));
const sample = Number(arg("sample", 8000));
const probe = arg("probe", "wgrender-host.js");
const url = arg("url", "/");
const guest = arg("guest", "WgrGuest");

const hook = `(() => {
    const calls = globalThis.__wgrCalls = {};
    globalThis.__wgrFrames = 0;
    (function tick() { globalThis.__wgrFrames++; requestAnimationFrame(tick); })();
    let value;
    Object.defineProperty(globalThis, ${JSON.stringify(guest)}, {
        configurable: true,
        get: () => value,
        set: (g) => {
            const start = g.start;
            g.start = (host, ...rest) => {
                for (const k of Object.keys(host)) {
                    if (!k.startsWith("_wgr_") || typeof host[k] !== "function") continue;
                    const f = host[k];
                    calls[k] = 0;
                    host[k] = (...a) => { calls[k]++; return f(...a); };
                }
                return start.call(g, host, ...rest);
            };
            value = g;
        },
    });
})();`;

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
    await page.send("Page.addScriptToEvaluateOnNewDocument", { source: hook });
    await page.send("Page.navigate", { url: `http://127.0.0.1:${port}${url}` });
    const read = async () => JSON.parse((await page.send("Runtime.evaluate", {
        expression: "JSON.stringify({ calls: globalThis.__wgrCalls, frames: globalThis.__wgrFrames })",
        returnByValue: true })).result.value);
    await sleep(warmup);
    const before = await read();
    await sleep(sample);
    const after = await read();
    const frames = after.frames - before.frames;
    if (Object.keys(after.calls).length === 0) {
        console.error(`${label}: no _wgr_ calls seen — is ${guest}.start how this page boots?`);
        process.exit(1);
    }
    const perFrame = {};
    for (const [k, n] of Object.entries(after.calls)) {
        const d = n - (before.calls[k] ?? 0);
        if (d) perFrame[k.slice(1)] = +(d / frames).toFixed(2);
    }
    const total = Object.values(perFrame).reduce((a, b) => a + b, 0);
    console.log(JSON.stringify({ label, frames, callsPerFrame: +total.toFixed(2), perFrame }));
} finally { await run.stop(); }
