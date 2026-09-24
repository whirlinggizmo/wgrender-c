#!/usr/bin/env node
// Web startup: how long each example takes to start, the first time and after.
//
//   node tools/webstart.mjs [options] [example ...]     (after building the web preset)
//
// Each example is opened three times in a fresh browser profile, served the way a
// typical host serves it (tools/serve.py --cache --gzip: files kept and revalidated,
// compressed):
//   cold   nothing cached: everything downloads and compiles
//   warm   the second visit: code from the HTTP cache (revalidated), assets from
//          libwgrender's IndexedDB file cache
//   hot    the third: Chrome keeps compiled wasm from the second visit on (its code
//          cache), so this is the best a returning visit gets
// and each is timed from navigation to:
//   js, wasm   the example's JS glue and wasm downloaded (with the bytes transferred)
//   compiled   the wasm compiled and instantiated (WebAssembly.instantiate*); it
//              streams, so it overlaps the download
//   init       libwgrender's init (worker threads started, main() run, the graphics device
//              made, the first animation frame): "wgr:init"
//   libwgrender      libwgrender's subsystems set up (shaders, pipelines, pools): "wgr:subsystems"
//   user       the program's init callback done: "wgr:user-init"
//   fs         the IndexedDB file cache opened (its list of files): "wgr:fs-ready"
//   frame      the first frame drawn: "wgr:first-frame"
//   ready      the first frame with no asset loads pending
// (the wgr:* points are performance marks libwgrender makes in web builds).
//
// Options:
//   --backend=webgl2|webgpu   (default webgl2); the site is build/web-<backend>
//   --threads=0               the -nothreads build
//   --net=none|4g|both        network: the local machine as is, emulated 4G (9 Mbit/s
//                             down, 150 ms round trips), or both (default both)
//   --headless                WebGL2 on SwiftShader (a CPU renderer) instead of the GPU
//                             on a virtual X display; its compile times aren't a GPU's
//   --browser=PATH            (or WEBCHECK_BROWSER)
//   --json=FILE               also write the numbers as JSON
//
// Another device (a phone): attach to its browser instead of starting one, and serve
// the site where it can reach it:
//   --devtools=PORT           the browser's DevTools on this machine (Android:
//                             adb forward tcp:PORT localabstract:chrome_devtools_remote)
//   --url=URL                 the site's address as that device reaches it, e.g.
//                             https://192.168.1.200:8443 (served here on that port)
//   --tls=CERT,KEY            serve HTTPS (threaded builds need a secure page there)
// Its profile isn't fresh, so a cold visit clears the site's cache and storage first;
// the GPU driver's own shader cache stays.
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { connect } from "node:net";
import { join } from "node:path";

import { findBrowser, findXvfb, freePort, launchBrowser, openSession, ROOT, RunProcesses, sleep, waitFor }
    from "./weblib.mjs";

const NETS = {
    none: null,
    "4g": { offline: false, latency: 150, downloadThroughput: (9e6 / 8), uploadThroughput: (1.5e6 / 8) },
};
const VISITS = ["cold", "warm", "hot"];
const WARM_UP = {
    webgl2: "const g=document.getElementById('c').getContext('webgl2');g.clearColor(1,0,0,1);g.clear(g.COLOR_BUFFER_BIT);",
    webgpu: "navigator.gpu.requestAdapter().then((a)=>a.requestDevice());",
};
const READY_TIMEOUT_MS = 60000;

/* Runs in the page before its own scripts: marks when the wasm is being compiled and
 * instantiated ("wasm:start", "wasm:ready"), and the first frame after which no asset
 * load is pending ("wgr:ready"). */
const PAGE_PROBE = `(() => {
    for (const name of ["instantiateStreaming", "instantiate"]) {
        const original = WebAssembly[name];
        WebAssembly[name] = function (...args) {
            if (performance.getEntriesByName("wasm:start").length === 0) performance.mark("wasm:start");
            return original.apply(this, args).then((result) => {
                if (performance.getEntriesByName("wasm:ready").length === 0) performance.mark("wasm:ready");
                return result;
            });
        };
    }
    const timer = setInterval(() => {
        if (performance.getEntriesByName("wgr:first-frame").length === 0) return;
        const m = globalThis.Module;
        const pending = m && m._wgri_asset_pending_count ? m._wgri_asset_pending_count() : -1;
        if (pending === 0) { performance.mark("wgr:ready"); clearInterval(timer); }
    }, 5);
})();`;

function parseArgs(argv) {
    const opts = { backend: "webgl2", threads: true, nets: ["none", "4g"], headless: false,
                   browser: process.env.WEBCHECK_BROWSER, json: null, devtools: null, url: null, tls: null,
                   examples: [] };
    for (const arg of argv) {
        const [key, value] = arg.split(/=(.*)/s);
        switch (key) {
            case "--backend": opts.backend = value; break;
            case "--threads": opts.threads = value !== "0"; break;
            case "--net": opts.nets = value === "both" ? ["none", "4g"] : [value]; break;
            case "--headless": opts.headless = true; break;
            case "--browser": opts.browser = value; break;
            case "--json": opts.json = value; break;
            case "--devtools": opts.devtools = Number(value); break;
            case "--url": opts.url = value.replace(/\/$/, ""); break;
            case "--tls": opts.tls = value.split(","); break;
            default:
                if (arg.startsWith("--")) throw new Error(`unknown option ${arg}`);
                opts.examples.push(arg);
        }
    }
    if (!["webgl2", "webgpu"].includes(opts.backend)) throw new Error(`--backend must be webgl2 or webgpu`);
    for (const net of opts.nets) if (!(net in NETS)) throw new Error(`--net must be none, 4g or both`);
    if (opts.headless && opts.backend === "webgpu") throw new Error("--headless has no WebGPU");
    if ((opts.devtools === null) !== (opts.url === null)) throw new Error("--devtools and --url go together");
    opts.display = opts.devtools !== null ? "remote" : opts.headless ? "headless" : findXvfb() ? "xvfb" : "screen";
    opts.site = join(ROOT, "build", `web-${opts.backend}${opts.threads ? "" : "-nothreads"}`);
    return opts;
}

async function waitForPort(port) {
    for (let i = 0; i < 100; i++) {
        const open = await new Promise((res) => {
            const socket = connect(port, "127.0.0.1", () => { socket.end(); res(true); });
            socket.on("error", () => res(false));
        });
        if (open) return;
        await sleep(100);
    }
    throw new Error(`tools/serve.py did not start (port ${port})`);
}

/* One visit: navigate, wait until ready (or the timeout), read the page's timings. */
async function visit(session, url) {
    await session.send("Page.navigate", { url });
    const deadline = Date.now() + READY_TIMEOUT_MS;
    let timings = null;
    while (Date.now() < deadline) {
        await sleep(100);
        const { result } = await session.send("Runtime.evaluate", {
            returnByValue: true,
            expression: `(() => {
                if (!location.search.includes("ex=")) return null;
                const at = (name) => { const e = performance.getEntriesByName(name); return e.length ? e[0].startTime : null; };
                const file = (suffix) => {
                    const e = performance.getEntriesByType("resource").find((r) => new URL(r.name).pathname.endsWith(suffix));
                    return e ? { end: e.responseEnd, transferred: e.transferSize, size: e.decodedBodySize } : null;
                };
                const name = new URLSearchParams(location.search).get("ex");
                return { compile: at("wasm:start"), compiled: at("wasm:ready"), ready: at("wgr:ready"), init: at("wgr:init"), subsystems: at("wgr:subsystems"), user: at("wgr:user-init"), fs: at("wgr:fs-ready"),
                         frame: at("wgr:first-frame"), js: file("/" + name + ".js"), wasm: file("/" + name + ".wasm") };
            })()`,
        });
        timings = result.value;
        if (timings && timings.ready !== null) break;
    }
    return timings;
}

async function measure(run, browserPath, baseUrl, example, net, opts) {
    const remote = opts.display === "remote";
    const profile = join(run.profile, `${example}-${net}`);
    const { debugBase, browser } = remote
        ? { debugBase: `http://127.0.0.1:${opts.devtools}`, browser: null }
        : await launchBrowser(run, browserPath, { display: opts.display, backend: opts.backend, profile });
    try {
        const targets = await (await fetch(`${debugBase}/json/list`)).json();
        const page = targets.find((t) => t.type === "page"); /* the tab in front */
        if (!page) throw new Error(`no page to use at ${debugBase}`);
        const session = await openSession(page.webSocketDebuggerUrl);
        await session.send("Page.enable");
        await session.send("Network.enable");
        if (remote) { /* not a fresh profile: forget the site */
            await session.send("Network.clearBrowserCache", {}, 60000); /* slow on a phone */
            await session.send("Storage.clearDataForOrigin", { origin: new URL(baseUrl).origin, storageTypes: "all" });
        }
        await session.send("Network.emulateNetworkConditions",
                           NETS[net] ?? { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 });
        /* a visitor's browser is already running: start its GPU process and graphics
           device first, on an unrelated page, so the cold visit doesn't pay for that */
        await session.send("Page.navigate", { url: `data:text/html,<canvas id=c></canvas><script>${WARM_UP[opts.backend]}</script>` });
        await sleep(2000);
        const probe = await session.send("Page.addScriptToEvaluateOnNewDocument", { source: PAGE_PROBE });
        const visits = {};
        for (const name of VISITS) {
            visits[name] = await visit(session, `${baseUrl}/?ex=${encodeURIComponent(example)}`);
            /* let the browser finish writing: libwgrender's file cache (IndexedDB) and the
               compiled wasm (the code cache) are written after the page is up */
            await session.send("Page.navigate", { url: "about:blank" });
            await sleep(1500);
        }
        await session.send("Page.removeScriptToEvaluateOnNewDocument", { identifier: probe.identifier });
        session.close();
        return visits;
    } finally {
        if (browser) { /* each example has its own browser: stop it, so it can't slow the next one */
            await browser.send("Browser.close").catch(() => {});
            browser.close();
            await sleep(500);
        }
    }
}

const ms = (v) => (v === null || v === undefined ? "-" : String(Math.round(v)));
const kb = (file) => {
    if (!file) return "-";
    /* a revalidated file transfers only its headers */
    return file.transferred < 1024 ? "cached" : `${Math.round(file.transferred / 1024)} KB`;
};

async function main() {
    if (typeof WebSocket === "undefined") throw new Error(`Node ${process.version} has no built-in WebSocket; needs Node >= 22`);
    const opts = parseArgs(process.argv.slice(2));
    const manifest = join(opts.site, "examples.json");
    if (!existsSync(manifest)) throw new Error(`no web build at ${opts.site}`);
    const built = JSON.parse(readFileSync(manifest, "utf8"));
    const examples = opts.examples.length ? opts.examples : built;
    const missing = examples.filter((e) => !built.includes(e));
    if (missing.length) throw new Error(`not built: ${missing.join(", ")}`);

    const browserPath = opts.display === "remote" ? null : findBrowser(opts.browser);
    const run = new RunProcesses("webstart");
    process.on("exit", () => run.stopNow());
    for (const [signal, code] of [["SIGINT", 130], ["SIGTERM", 143], ["SIGHUP", 129]]) {
        process.on(signal, () => { run.stop().finally(() => process.exit(code)); });
    }
    const results = {};
    try {
        const sitePort = opts.url ? Number(new URL(opts.url).port) : await freePort();
        run.spawn("python3", [join(ROOT, "tools", "serve.py"), String(sitePort), opts.site, "--cache", "--gzip",
                              ...(opts.tls ? ["--tls", ...opts.tls] : [])]);
        const baseUrl = opts.url ?? `http://127.0.0.1:${sitePort}`;
        if (opts.tls) await waitForPort(sitePort); /* its certificate isn't for 127.0.0.1 */
        else await waitFor(`${baseUrl}/examples.json`, "tools/serve.py");
        const where = { headless: "headless (SwiftShader)", xvfb: "GPU, virtual display (Xvfb)", screen: "GPU, on the screen",
                        remote: `the browser at DevTools port ${opts.devtools}` };
        console.log(`webstart: ${examples.length} example(s), ${opts.backend}${opts.threads ? "" : " no threads"}, ` +
                    `${where[opts.display]}${browserPath ? `, ${browserPath}` : ""}`);
        console.log("ms from navigation; js and wasm show the bytes transferred (gzip)\n");
        for (const net of opts.nets) {
            console.log(`network: ${net === "none" ? "local (no throttling)" : "emulated 4G (9 Mbit/s, 150 ms)"}`);
            console.log(`${"example".padEnd(14)} ${"visit".padEnd(5)} ${"js".padStart(6)} ${"(xfer)".padStart(9)} ` +
                        `${"wasm".padStart(6)} ${"(xfer)".padStart(9)} ${"compiled".padStart(8)} ${"init".padStart(6)} ${"libwgrender".padStart(6)} ` +
                        `${"user".padStart(6)} ${"fs".padStart(6)} ` +
                        `${"frame".padStart(6)} ${"ready".padStart(6)}`);
            for (const example of examples) {
                let visits;
                try {
                    visits = await measure(run, browserPath, baseUrl, example, net, opts);
                } catch (err) {
                    console.log(`${example.padEnd(14)} failed: ${err.message}`);
                    continue;
                }
                (results[example] ??= {})[net] = visits;
                for (const name of VISITS) {
                    const t = visits[name] || {};
                    console.log(`${(name === "cold" ? example : "").padEnd(14)} ${name.padEnd(5)} ` +
                                `${ms(t.js?.end).padStart(6)} ${kb(t.js).padStart(9)} ` +
                                `${ms(t.wasm?.end).padStart(6)} ${kb(t.wasm).padStart(9)} ${ms(t.compiled).padStart(8)} ` +
                                `${ms(t.init).padStart(6)} ${ms(t.subsystems).padStart(6)} ${ms(t.user).padStart(6)} ${ms(t.fs).padStart(6)} ${ms(t.frame).padStart(6)} ` +
                                `${(t.ready === null || t.ready === undefined ? "timeout" : ms(t.ready)).padStart(6)}`);
                }
            }
            console.log("");
        }
        if (opts.json) writeFileSync(opts.json, JSON.stringify({ backend: opts.backend, threads: opts.threads, results }, null, 2));
        return 0;
    } finally {
        await run.stop();
    }
}

main().then((code) => process.exit(code), (err) => {
    console.error(`webstart: ${err.message}`);
    process.exit(2);
});
