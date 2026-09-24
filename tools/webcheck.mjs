#!/usr/bin/env node
// Web smoke check for the libwgrender examples (tools/verify.py --web runs it).
//
// Serves build/web-<backend> with tools/serve.py, loads each built example in a
// Chromium-based browser (Brave, Chrome, Chromium) through the DevTools
// protocol, and fails an example if it logs a console error or a libwgrender
// [ERROR]/[FATAL] line, throws, hits a sokol panic, never reports starting on the
// expected backend, or is still loading assets when its time runs out. A screenshot
// of every example is saved for a visual check.
//
// No npm dependencies: needs Node >= 22 (built-in fetch and WebSocket).
//
//   node tools/webcheck.mjs [options] [example ...]     (default: all built examples)
//
//   --backend=webgl2|webgpu  backend to check (default webgl2); the site is
//                       build/web-<backend> (the CMake preset of that name)
//   --headed            show the browser window on the real screen. Otherwise WebGL2
//                       runs headless, and WebGPU (which gets no working GPU device
//                       headless) runs on a private virtual X display (Xvfb, ANGLE on
//                       Vulkan), so it never shows a window or wakes the monitors. Without
//                       Xvfb installed, WebGPU falls back to the real screen.
//   --settle=MS         longest an example runs before it is checked (default 20000). An
//                       example is checked once it has started, has no asset tasks
//                       pending (libwgrender's queue) and no network requests in flight, and
//                       has run --quiet ms since the last of those changed
//   --quiet=MS          (default 1500)
//   --jobs=N            examples checked at once (default 4). Each example has its own
//                       browser context (own storage; its own window when headed)
//   --out=DIR           screenshot directory (default build/web-<backend>/webcheck)
//   --browser=PATH      browser executable (or WEBCHECK_BROWSER; default: first
//                       found of brave-browser-stable, google-chrome-stable,
//                       google-chrome, chromium, chromium-browser)
//   --verbose           print every console line and browser log entry an example
//                       produced (WebGPU validation messages arrive as log entries,
//                       not console calls, so this is how to see them)
//
// Cleanup: the browser and server are always stopped, including when this script
// crashes or is killed (see RunProcesses in weblib.mjs).
//
// Note: never call canvas.getContext() from here. A canvas that already has a
// WebGL context can't be used for WebGPU, which breaks the example under test.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { basename, join } from "node:path";

import { findBrowser, findXvfb, freePort, launchBrowser, openSession, ROOT, RunProcesses, sleep, waitFor } from "./weblib.mjs";

const BACKEND_LOG = { webgl2: "GLES3/WebGL2 backend", webgpu: "WebGPU backend" };

function parseArgs(argv) {
    const opts = { backend: "webgl2", headed: false, settle: 20000, quiet: 1500, jobs: 0, out: null,
                   browser: process.env.WEBCHECK_BROWSER, threads: true, verbose: false, examples: [] };
    for (const arg of argv) {
        const [key, value] = arg.split(/=(.*)/s);
        switch (key) {
            case "--backend": opts.backend = value; break;
            case "--headed": opts.headed = true; break;
            case "--settle": opts.settle = Number(value); break;
            case "--quiet": opts.quiet = Number(value); break;
            case "--jobs": opts.jobs = Number(value); break;
            case "--out": opts.out = value; break;
            case "--browser": opts.browser = value; break;
            case "--threads": opts.threads = value !== "0"; break;
            case "--verbose": opts.verbose = true; break;
            default:
                if (arg.startsWith("--")) throw new Error(`unknown option ${arg}`);
                opts.examples.push(arg);
        }
    }
    if (!(opts.backend in BACKEND_LOG)) throw new Error(`--backend must be webgl2 or webgpu, got '${opts.backend}'`);
    /* where the browser shows its windows: "headless", "xvfb" or "screen" */
    opts.display = opts.headed ? "screen" : opts.backend === "webgpu" ? (findXvfb() ? "xvfb" : "screen") : "headless";
    opts.site = join(ROOT, "build", `web-${opts.backend}${opts.threads ? "" : "-nothreads"}`);
    if (!(opts.jobs >= 1)) opts.jobs = 4;
    opts.out ??= join(opts.site, "webcheck");
    return opts;
}

async function checkExample(browser, debugBase, baseUrl, example, opts) {
    const result = { example, errors: [], started: false, backendOk: false, screenshot: null, console: [] };
    /* Each example gets its own browser context (separate storage, like a fresh
     * profile), so examples checked at the same time don't share the IndexedDB
     * file cache and can't slow or affect each other. */
    const { browserContextId } = await browser.send("Target.createBrowserContext", { disposeOnDetach: true });
    const { targetId } = await browser.send("Target.createTarget", { url: "about:blank", browserContextId });
    const session = await openSession(`ws://${new URL(debugBase).host}/devtools/page/${targetId}`);
    try {
        const inflight = new Set();
        let navigated = Date.now();
        let lastActivity = Date.now();
        session.onEvent((msg) => {
            if (msg.method === "Network.requestWillBeSent") {
                /* a worker's own script (threaded builds start pthread workers) has no
                 * loader, and its completion is reported to the worker, not this page */
                if (!msg.params.loaderId) return;
                inflight.add(msg.params.requestId);
                lastActivity = Date.now();
            } else if (msg.method === "Network.loadingFinished" || msg.method === "Network.loadingFailed") {
                inflight.delete(msg.params.requestId);
                lastActivity = Date.now();
            } else if (msg.method === "Runtime.consoleAPICalled") {
                const text = msg.params.args.map((a) => a.value ?? a.description ?? "").join(" ");
                result.console.push(text.trim().split("\n")[0]);
                if (text.includes("libwgrender:") && text.includes("backend")) {
                    result.started = true;
                    result.startMs ??= Date.now() - navigated;
                    lastActivity = Date.now();
                    result.backendOk ||= text.includes(BACKEND_LOG[opts.backend]);
                }
                /* libwgrender logs go to the console as plain messages: fail on error-level
                 * ones like tools/smoke.sh does ([ERROR], [FATAL]) */
                if (msg.params.type === "error" || text.includes("[panic]") || /\[(ERROR|FATAL)/.test(text)) {
                    result.errors.push(text.trim().split("\n")[0]);
                }
            } else if (msg.method === "Runtime.exceptionThrown") {
                const d = msg.params.exceptionDetails;
                result.errors.push((d.exception?.description || d.text || "exception").split("\n")[0]);
            } else if (msg.method === "Log.entryAdded") {
                /* the browser's own messages: WebGPU validation errors from Dawn, GL
                 * driver warnings, network failures. Error-level ones fail the check,
                 * except network ones: a missing favicon is a 404 too, and a missing
                 * asset already fails through libwgrender's own loading errors */
                const e = msg.params.entry;
                const line = `[${e.source}/${e.level}] ${(e.text || "").trim().split("\n")[0]}`;
                result.console.push(line);
                if (e.level === "error" && e.source !== "network") result.errors.push(line);
            }
        });
        await session.send("Runtime.enable");
        await session.send("Log.enable");
        await session.send("Page.enable");
        await session.send("Network.enable");
        const deadline = Date.now() + opts.settle;
        navigated = Date.now();
        await session.send("Page.navigate", { url: `${baseUrl}/?ex=${encodeURIComponent(example)}` });
        /* Wait until the example has started, its asset downloads are done, and it has
         * run for a while since (errors from loading show up in that window), or until
         * the deadline, whichever comes first. */
        let pending = -1;
        while (Date.now() < deadline) {
            if (!result.started) {
                /* don't call into the page before libwgrender reports it's running: calling an
                 * exported function before the wasm runtime is initialized aborts the page */
                await sleep(100);
                continue;
            }
            const { result: value } = await session.send("Runtime.evaluate", {
                expression: "typeof Module !== 'undefined' && Module._wgri_asset_pending_count ? Module._wgri_asset_pending_count() : -1",
                returnByValue: true,
            });
            if (value.value !== pending) {
                pending = value.value;
                lastActivity = Date.now();
            }
            if (result.started && pending === 0 && inflight.size === 0 && Date.now() - lastActivity >= opts.quiet) {
                break;
            }
            await sleep(100);
        }
        result.pending = pending;
        if (pending > 0) { /* out of time: have libwgrender say which files, and where they are stuck */
            await session.send("Runtime.evaluate", {
                expression: "typeof Module !== 'undefined' && Module._wgri_asset_pending_log && Module._wgri_asset_pending_log()",
            }).catch(() => {});
            await sleep(300); /* the warnings arrive as console events */
        }
        const shot = await session.send("Page.captureScreenshot", { format: "png" });
        result.screenshot = join(opts.out, `${example}.png`);
        writeFileSync(result.screenshot, Buffer.from(shot.data, "base64"));
    } finally {
        session.close();
        await browser.send("Target.closeTarget", { targetId }).catch(() => {});
        await browser.send("Target.disposeBrowserContext", { browserContextId }).catch(() => {});
    }
    return result;
}

async function main() {
    if (typeof WebSocket === "undefined") {
        throw new Error(`Node ${process.version} has no built-in WebSocket; webcheck needs Node >= 22`);
    }
    const opts = parseArgs(process.argv.slice(2));
    const manifest = join(opts.site, "examples.json");
    if (!existsSync(manifest)) {
        throw new Error(`no web build at ${opts.site} (build it first: cmake --preset ${basename(opts.site)} && cmake --build --preset ${basename(opts.site)})`);
    }
    const built = JSON.parse(readFileSync(manifest, "utf8"));
    const examples = opts.examples.length ? opts.examples : built;
    const missing = examples.filter((e) => !built.includes(e));
    if (missing.length) throw new Error(`not built: ${missing.join(", ")}`);

    const browserPath = findBrowser(opts.browser);
    mkdirSync(opts.out, { recursive: true });
    const run = new RunProcesses("webcheck");
    process.on("exit", () => run.stopNow());
    for (const [signal, code] of [["SIGINT", 130], ["SIGTERM", 143], ["SIGHUP", 129]]) {
        process.on(signal, () => { run.stop().finally(() => process.exit(code)); });
    }
    process.on("uncaughtException", (err) => {
        console.error(`webcheck: ${err.stack || err.message}`);
        run.stopNow();
        process.exit(2);
    });

    try {
        const sitePort = await freePort();
        run.spawn("python3", [join(ROOT, "tools", "serve.py"), String(sitePort), opts.site]);
        const baseUrl = `http://127.0.0.1:${sitePort}`;
        await waitFor(`${baseUrl}/examples.json`, "tools/serve.py");

        if (opts.display === "screen" && opts.backend === "webgpu" && !opts.headed) {
            console.log("webcheck: Xvfb not found; WebGPU runs in a window on the real screen");
        }
        const { debugBase, browser } = await launchBrowser(run, browserPath, { display: opts.display, backend: opts.backend });

        console.log(`webcheck: ${examples.length} example(s), backend ${opts.backend}, ` +
                    `${opts.display === "xvfb" ? "virtual display (Xvfb)" : opts.display}, ${opts.jobs} at a time, ${browserPath}`);
        /* check `jobs` examples at a time; report in order as results complete */
        const results = new Array(examples.length);
        let next = 0, reported = 0, failed = 0;
        const report = () => {
            while (reported < examples.length && results[reported]) {
                const r = results[reported++];
                const problems = [...r.errors];
                if (!r.started) {
                    problems.push("never logged its backend (did not start?); last console output:");
                    for (const line of r.console.slice(-6)) problems.push(`  | ${line}`);
                }
                else if (r.pending < 0) problems.push(`never reported its asset queue in ${opts.settle} ms (the runtime was still starting)`);
                else if (r.pending !== 0) {
                    problems.push(`still loading after ${opts.settle} ms (${r.pending} asset task(s) pending):`);
                    const stuck = r.console.filter((line) => line.includes("wgr_asset: pending:"));
                    for (const line of (stuck.length ? stuck : r.console.slice(-6))) problems.push(`  | ${line}`);
                }
                else if (!r.backendOk) problems.push(`started on a different backend than '${opts.backend}' (stale build?)`);
                if (problems.length) failed++;
                const start = r.startMs !== undefined ? ` (started after ${(r.startMs / 1000).toFixed(1)} s)` : "";
                console.log(`  ${problems.length ? "FAIL" : "ok  "}  ${r.example}${problems.length ? start : ""}`);
                for (const p of problems) console.log(`          ${p}`);
                if (opts.verbose) for (const line of r.console) console.log(`          | ${line}`);
            }
        };
        const worker = async () => {
            while (next < examples.length) {
                const index = next++;
                try {
                    results[index] = await checkExample(browser, debugBase, baseUrl, examples[index], opts);
                } catch (err) {
                    results[index] = { example: examples[index], errors: [`check failed: ${err.message}`],
                                       started: true, backendOk: true, pending: 0 };
                }
                report();
            }
        };
        await Promise.all(Array.from({ length: Math.min(opts.jobs, examples.length) }, worker));
        browser.close();
        console.log(`screenshots: ${opts.out}`);
        console.log(failed ? `FAIL: ${failed} of ${examples.length} example(s)` : `PASS: ${examples.length} example(s)`);
        return failed ? 1 : 0;
    } finally {
        await run.stop();
    }
}

main().then((code) => process.exit(code), (err) => {
    console.error(`webcheck: ${err.message}`);
    process.exit(2);
});
