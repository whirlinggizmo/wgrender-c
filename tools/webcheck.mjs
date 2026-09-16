#!/usr/bin/env node
// Web smoke check for the libsk examples (`make webcheck`).
//
// Serves examples/build/web with tools/serve.py, loads each built example in a
// Chromium-based browser (Brave, Chrome, Chromium) through the DevTools
// protocol, and fails an example if it logs a console error, throws, hits a
// sokol panic, or never reports starting on the expected backend. A screenshot
// of every example is saved for a visual check.
//
// No npm dependencies: needs Node >= 22 (built-in fetch and WebSocket).
//
//   node tools/webcheck.mjs [options] [example ...]     (default: all built examples)
//
//   --backend=gl|wgpu   backend the build was made with (default gl)
//   --headed            show the browser window. WebGPU always runs headed:
//                       headless browsers have no GPU adapter.
//   --settle=MS         time each example runs before it is checked (default 5000)
//   --out=DIR           screenshot directory (default examples/build/webcheck/<backend>)
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
const SITE = join(ROOT, "examples", "build", "web");
const BACKEND_LOG = { gl: "GLES3/WebGL2 backend", wgpu: "WebGPU backend" };
const BROWSERS = ["brave-browser-stable", "google-chrome-stable", "google-chrome", "chromium", "chromium-browser"];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function parseArgs(argv) {
    const opts = { backend: "gl", headed: false, settle: 5000, out: null, browser: process.env.WEBCHECK_BROWSER, examples: [] };
    for (const arg of argv) {
        const [key, value] = arg.split(/=(.*)/s);
        switch (key) {
            case "--backend": opts.backend = value; break;
            case "--headed": opts.headed = true; break;
            case "--settle": opts.settle = Number(value); break;
            case "--out": opts.out = value; break;
            case "--browser": opts.browser = value; break;
            default:
                if (arg.startsWith("--")) throw new Error(`unknown option ${arg}`);
                opts.examples.push(arg);
        }
    }
    if (!(opts.backend in BACKEND_LOG)) throw new Error(`--backend must be gl or wgpu, got '${opts.backend}'`);
    if (opts.backend === "wgpu") opts.headed = true;
    opts.out ??= join(ROOT, "examples", "build", "webcheck", opts.backend);
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
    await new Promise((res, rej) => { ws.addEventListener("open", res); ws.addEventListener("error", rej); });
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
        send(method, params = {}) {
            return new Promise((res, rej) => {
                const id = ++nextId;
                pending.set(id, (msg) => (msg.error ? rej(new Error(`${method}: ${msg.error.message}`)) : res(msg.result)));
                ws.send(JSON.stringify({ id, method, params }));
            });
        },
        onEvent(fn) { listeners.push(fn); },
        close() { ws.close(); },
    };
}

async function checkExample(debugBase, baseUrl, example, opts) {
    const result = { example, errors: [], started: false, backendOk: false, screenshot: null };
    const target = await (await fetch(`${debugBase}/json/new?about:blank`, { method: "PUT" })).json();
    const session = await openSession(target.webSocketDebuggerUrl);
    try {
        session.onEvent((msg) => {
            if (msg.method === "Runtime.consoleAPICalled") {
                const text = msg.params.args.map((a) => a.value ?? a.description ?? "").join(" ");
                if (text.includes("libsk:") && text.includes("backend")) {
                    result.started = true;
                    result.backendOk ||= text.includes(BACKEND_LOG[opts.backend]);
                }
                if (msg.params.type === "error" || text.includes("[panic]")) {
                    result.errors.push(text.trim().split("\n")[0]);
                }
            } else if (msg.method === "Runtime.exceptionThrown") {
                const d = msg.params.exceptionDetails;
                result.errors.push((d.exception?.description || d.text || "exception").split("\n")[0]);
            }
        });
        await session.send("Runtime.enable");
        await session.send("Page.enable");
        await session.send("Page.navigate", { url: `${baseUrl}/?ex=${encodeURIComponent(example)}` });
        await sleep(opts.settle);
        const shot = await session.send("Page.captureScreenshot", { format: "png" });
        result.screenshot = join(opts.out, `${example}.png`);
        writeFileSync(result.screenshot, Buffer.from(shot.data, "base64"));
    } finally {
        session.close();
        await fetch(`${debugBase}/json/close/${target.id}`).catch(() => {});
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
    const manifest = join(SITE, "examples.json");
    if (!existsSync(manifest)) {
        throw new Error(`no web build at ${SITE} (run 'make wasm-all BACKEND=${opts.backend}' first)`);
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
        run.spawn("python3", [join(ROOT, "tools", "serve.py"), String(sitePort)]);
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
        await waitFor(`${debugBase}/json/version`, "browser");

        console.log(`webcheck: ${examples.length} example(s), backend ${opts.backend}, ` +
                    `${opts.headed ? "headed" : "headless"}, ${browserPath}`);
        let failed = 0;
        for (const example of examples) {
            const r = await checkExample(debugBase, baseUrl, example, opts);
            const problems = [...r.errors];
            if (!r.started) problems.push("never logged its backend (did not start?)");
            else if (!r.backendOk) problems.push(`started on a different backend than '${opts.backend}' (stale build?)`);
            if (problems.length) failed++;
            console.log(`  ${problems.length ? "FAIL" : "ok  "}  ${example}`);
            for (const p of problems) console.log(`          ${p}`);
        }
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
