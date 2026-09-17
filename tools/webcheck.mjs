#!/usr/bin/env node
// Web smoke check for the libsk examples (`make webcheck`).
//
// Serves examples/build/<backend> with tools/serve.py, loads each built example in a
// Chromium-based browser (Brave, Chrome, Chromium) through the DevTools
// protocol, and fails an example if it logs a console error or a libsk
// [ERROR]/[FATAL] line, throws, hits a sokol panic, never reports starting on the
// expected backend, or is still loading assets when its time runs out. A screenshot
// of every example is saved for a visual check.
//
// No npm dependencies: needs Node >= 22 (built-in fetch and WebSocket).
//
//   node tools/webcheck.mjs [options] [example ...]     (default: all built examples)
//
//   --backend=webgl2|webgpu  backend to check (default webgl2); the site is
//                       examples/build/<backend>
//   --headed            show the browser window. WebGPU always runs headed:
//                       headless browsers have no GPU adapter.
//   --settle=MS         longest an example runs before it is checked (default 20000). An
//                       example is checked once it has started, has no asset tasks
//                       pending (libsk's queue) and no network requests in flight, and
//                       has run --quiet ms since the last of those changed
//   --quiet=MS          (default 1500)
//   --jobs=N            examples checked at once (default 4). Each example has its own
//                       browser context (own storage; its own window when headed)
//   --out=DIR           screenshot directory (default examples/build/<backend>/webcheck)
//   --browser=PATH      browser executable (or WEBCHECK_BROWSER; default: first
//                       found of brave-browser-stable, google-chrome-stable,
//                       google-chrome, chromium, chromium-browser)
//
// Cleanup: the browser and server are always stopped, including when this script
// crashes or is killed (see RunProcesses below).
//
// Note: never call canvas.getContext() from here. A canvas that already has a
// WebGL context can't be used for WebGPU, which breaks the example under test.
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const BACKEND_LOG = { webgl2: "GLES3/WebGL2 backend", webgpu: "WebGPU backend" };
const BROWSERS = ["brave-browser-stable", "google-chrome-stable", "google-chrome", "chromium", "chromium-browser"];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function parseArgs(argv) {
    const opts = { backend: "webgl2", headed: false, settle: 20000, quiet: 1500, jobs: 0, out: null,
                   browser: process.env.WEBCHECK_BROWSER, examples: [] };
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
            default:
                if (arg.startsWith("--")) throw new Error(`unknown option ${arg}`);
                opts.examples.push(arg);
        }
    }
    if (!(opts.backend in BACKEND_LOG)) throw new Error(`--backend must be webgl2 or webgpu, got '${opts.backend}'`);
    if (opts.backend === "webgpu") opts.headed = true;
    opts.site = join(ROOT, "examples", "build", opts.backend);
    if (!(opts.jobs >= 1)) opts.jobs = 4;
    opts.out ??= join(opts.site, "webcheck");
    return opts;
}

function findBrowser(explicit) {
    if (explicit) return explicit;
    for (const name of BROWSERS) {
        for (const dir of (process.env.PATH || "").split(":")) {
            if (dir && existsSync(join(dir, name))) return join(dir, name);
        }
    }
    throw new Error(`no Chromium-based browser found (tried ${BROWSERS.join(", ")}); set --browser or WEBCHECK_BROWSER`);
}

function freePort() {
    return new Promise((resolvePort, reject) => {
        const srv = createServer();
        srv.on("error", reject);
        srv.listen(0, "127.0.0.1", () => {
            const { port } = srv.address();
            srv.close(() => resolvePort(port));
        });
    });
}

async function waitFor(url, what) {
    for (let i = 0; i < 100; i++) {
        try {
            const res = await fetch(url);
            if (res.ok) return res;
        } catch { /* not up yet */ }
        await sleep(100);
    }
    throw new Error(`${what} did not start (${url})`);
}

// Minimal DevTools-protocol session on one page target.
async function openSession(wsUrl) {
    const ws = new WebSocket(wsUrl);
    await new Promise((res, rej) => {
        const timer = setTimeout(() => rej(new Error(`no DevTools connection to ${wsUrl} in 15000 ms`)), 15000);
        ws.addEventListener("open", () => { clearTimeout(timer); res(); });
        ws.addEventListener("error", (ev) => { clearTimeout(timer); rej(new Error(`DevTools connection failed: ${ev.message ?? wsUrl}`)); });
    });
    let nextId = 0;
    const pending = new Map();
    const listeners = [];
    ws.addEventListener("message", (ev) => {
        const msg = JSON.parse(ev.data);
        if (msg.id !== undefined && pending.has(msg.id)) {
            pending.get(msg.id)(msg);
            pending.delete(msg.id);
        } else if (msg.method) {
            for (const fn of listeners) fn(msg);
        }
    });
    return {
        /* Every request times out: a hung or crashed page must fail the check, not
         * stall the whole run. */
        send(method, params = {}, timeoutMs = 15000) {
            return new Promise((res, rej) => {
                const id = ++nextId;
                const timer = setTimeout(() => {
                    pending.delete(id);
                    rej(new Error(`${method}: no response from the browser in ${timeoutMs} ms`));
                }, timeoutMs);
                pending.set(id, (msg) => {
                    clearTimeout(timer);
                    msg.error ? rej(new Error(`${method}: ${msg.error.message}`)) : res(msg.result);
                });
                ws.send(JSON.stringify({ id, method, params }));
            });
        },
        onEvent(fn) { listeners.push(fn); },
        close() { ws.close(); },
    };
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
        let lastActivity = Date.now();
        session.onEvent((msg) => {
            if (msg.method === "Network.requestWillBeSent") {
                inflight.add(msg.params.requestId);
                lastActivity = Date.now();
            } else if (msg.method === "Network.loadingFinished" || msg.method === "Network.loadingFailed") {
                inflight.delete(msg.params.requestId);
                lastActivity = Date.now();
            } else if (msg.method === "Runtime.consoleAPICalled") {
                const text = msg.params.args.map((a) => a.value ?? a.description ?? "").join(" ");
                result.console.push(text.trim().split("\n")[0]);
                if (text.includes("libsk:") && text.includes("backend")) {
                    result.started = true;
                    lastActivity = Date.now();
                    result.backendOk ||= text.includes(BACKEND_LOG[opts.backend]);
                }
                /* libsk logs go to the console as plain messages: fail on error-level
                 * ones like tools/smoke.sh does ([ERROR], [FATAL]) */
                if (msg.params.type === "error" || text.includes("[panic]") || /\[(ERROR|FATAL)/.test(text)) {
                    result.errors.push(text.trim().split("\n")[0]);
                }
            } else if (msg.method === "Runtime.exceptionThrown") {
                const d = msg.params.exceptionDetails;
                result.errors.push((d.exception?.description || d.text || "exception").split("\n")[0]);
            }
        });
        await session.send("Runtime.enable");
        await session.send("Page.enable");
        await session.send("Network.enable");
        const deadline = Date.now() + opts.settle;
        await session.send("Page.navigate", { url: `${baseUrl}/?ex=${encodeURIComponent(example)}` });
        /* Wait until the example has started, its asset downloads are done, and it has
         * run for a while since (errors from loading show up in that window), or until
         * the deadline, whichever comes first. */
        let pending = -1;
        while (Date.now() < deadline) {
            if (!result.started) {
                /* don't call into the page before libsk reports it's running: calling an
                 * exported function before the wasm runtime is initialized aborts the page */
                await sleep(100);
                continue;
            }
            const { result: value } = await session.send("Runtime.evaluate", {
                expression: "typeof Module !== 'undefined' && Module._sk_asset_pending_count ? Module._sk_asset_pending_count() : -1",
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

// Everything this run starts, and how to stop all of it.
//
// Killing the spawned browser process isn't enough: Chromium-based browsers leave
// helper processes (zygotes, renderers, crashpad) that outlive the launcher. So:
//   - children start in their own process groups, and whole groups are stopped;
//   - every run has a unique profile directory, and any process whose command line
//     names it belongs to this run and is swept up afterwards;
//   - a detached watchdog shell waits for this Node process to disappear (a crash,
//     `kill -9`) and then does the same, so nothing leaks even if Node never gets
//     to run its cleanup. It is harmless when cleanup already ran.
class RunProcesses {
    constructor() {
        this.profile = mkdtempSync(join(tmpdir(), "libsk-webcheck-"));
        this.groups = [];
        this.stopped = false;
        const watchdog = spawn("sh", ["-c", `
            while kill -0 "$WEBCHECK_NODE_PID" 2>/dev/null; do sleep 1; done
            if [ -f "$WEBCHECK_PROFILE.groups" ]; then
                for g in $(cat "$WEBCHECK_PROFILE.groups"); do kill -9 "-$g" 2>/dev/null; done  # no "--": dash rejects it
            fi
            ps -eo pid=,comm=,args= | awk -v m="$WEBCHECK_PROFILE" '$2 != "sh" && $2 != "awk" && index($0, m) { print $1 }' |
                xargs -r kill -KILL 2>/dev/null
            rm -rf "$WEBCHECK_PROFILE" "$WEBCHECK_PROFILE.groups"
        `], {
            detached: true,
            stdio: "ignore",
            env: { ...process.env, WEBCHECK_NODE_PID: String(process.pid), WEBCHECK_PROFILE: this.profile },
        });
        watchdog.unref();
    }

    // Start a child in its own process group and record the group for the watchdog.
    spawn(command, args) {
        const child = spawn(command, args, { stdio: "ignore", detached: true });
        child.unref();
        if (child.pid) {
            this.groups.push(child.pid);
            writeFileSync(`${this.profile}.groups`, this.groups.join(" "));
        }
        return child;
    }

    // Processes (other than this one) whose command line names the run's profile.
    strays() {
        let listing = "";
        try {
            listing = execFileSync("ps", ["-eo", "pid=,args="], { encoding: "utf8" });
        } catch {
            return [];
        }
        return listing.split("\n")
            .filter((line) => line.includes(this.profile))
            .map((line) => Number.parseInt(line.trim(), 10))
            .filter((pid) => Number.isInteger(pid) && pid !== process.pid);
    }

    signalAll(signal) {
        for (const group of this.groups) {
            try { process.kill(-group, signal); } catch { /* already gone */ }
        }
        for (const pid of this.strays()) {
            try { process.kill(pid, signal); } catch { /* already gone */ }
        }
    }

    removeFiles() {
        rmSync(this.profile, { recursive: true, force: true });
        rmSync(`${this.profile}.groups`, { force: true });
    }

    // Normal path: ask politely, give the browser a moment, then force.
    async stop() {
        if (this.stopped) return;
        this.stopped = true;
        this.signalAll("SIGTERM");
        for (let i = 0; i < 20 && this.strays().length > 0; i++) {
            await sleep(100);
        }
        this.signalAll("SIGKILL");
        this.removeFiles();
    }

    // Last resort from process 'exit' handlers, where only synchronous work runs.
    stopNow() {
        if (this.stopped) return;
        this.stopped = true;
        this.signalAll("SIGKILL");
        this.removeFiles();
    }
}

async function main() {
    if (typeof WebSocket === "undefined") {
        throw new Error(`Node ${process.version} has no built-in WebSocket; webcheck needs Node >= 22`);
    }
    const opts = parseArgs(process.argv.slice(2));
    const manifest = join(opts.site, "examples.json");
    if (!existsSync(manifest)) {
        throw new Error(`no web build at ${opts.site} (run 'make wasm-all BACKEND=${opts.backend}' first)`);
    }
    const built = JSON.parse(readFileSync(manifest, "utf8"));
    const examples = opts.examples.length ? opts.examples : built;
    const missing = examples.filter((e) => !built.includes(e));
    if (missing.length) throw new Error(`not built: ${missing.join(", ")}`);

    const browserPath = findBrowser(opts.browser);
    mkdirSync(opts.out, { recursive: true });
    const run = new RunProcesses();
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

        const debugPort = await freePort();
        run.spawn(browserPath, [
            ...(opts.headed ? [] : ["--headless=new"]),
            `--remote-debugging-port=${debugPort}`,
            `--user-data-dir=${run.profile}`,
            "--no-first-run",
            "--no-default-browser-check",
            "--window-size=1024,900",
            "--autoplay-policy=no-user-gesture-required",
            ...(opts.headed ? [] : ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"]),
            "about:blank",
        ]);
        const debugBase = `http://127.0.0.1:${debugPort}`;
        const version = await (await waitFor(`${debugBase}/json/version`, "browser")).json();
        const browser = await openSession(version.webSocketDebuggerUrl);

        console.log(`webcheck: ${examples.length} example(s), backend ${opts.backend}, ` +
                    `${opts.headed ? "headed" : "headless"}, ${opts.jobs} at a time, ${browserPath}`);
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
                else if (r.pending !== 0) problems.push(`still loading after ${opts.settle} ms (${r.pending} asset task(s) pending)`);
                else if (!r.backendOk) problems.push(`started on a different backend than '${opts.backend}' (stale build?)`);
                if (problems.length) failed++;
                console.log(`  ${problems.length ? "FAIL" : "ok  "}  ${r.example}`);
                for (const p of problems) console.log(`          ${p}`);
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
