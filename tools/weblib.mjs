// Shared by the web tools (webcheck.mjs, webstart.mjs): finding and launching a
// Chromium-based browser (headless, on a virtual X display, or on the screen), a
// minimal DevTools-protocol session, and a record of every process a run starts so
// all of it is stopped, whatever happens to the run.
import { execFileSync, spawn } from "node:child_process";
import { closeSync, existsSync, mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { delimiter, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const WINDOWS = process.platform === "win32";
const BROWSERS = WINDOWS ? ["brave.exe", "chrome.exe", "msedge.exe"]
    : ["brave-browser-stable", "google-chrome-stable", "google-chrome", "chromium", "chromium-browser",
       "brave-browser", "microsoft-edge-stable", "microsoft-edge"];

/* Where the browsers install themselves on Windows and macOS, which isn't on PATH:
 * Brave, then Chrome, Chromium, then Edge (on every Windows 11). */
function installedBrowsers() {
    if (WINDOWS) {
        const roots = [process.env.ProgramFiles, process.env["ProgramFiles(x86)"], process.env.LOCALAPPDATA].filter(Boolean);
        const apps = ["BraveSoftware/Brave-Browser/Application/brave.exe", "Google/Chrome/Application/chrome.exe",
                      "Chromium/Application/chrome.exe", "Microsoft/Edge/Application/msedge.exe"];
        return apps.flatMap((app) => roots.map((root) => join(root, app)));
    }
    if (process.platform === "darwin") {
        return ["Brave Browser", "Google Chrome", "Chromium", "Microsoft Edge"]
            .map((app) => `/Applications/${app}.app/Contents/MacOS/${app}`);
    }
    return [];
}

/* The Python the tools run on: the one that started them (tools/verify.py, the benchmark
 * harness), else python3 or, on Windows, python. */
export const PYTHON = process.env.WGR_PYTHON || (WINDOWS ? "python" : "python3");

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function onPath(name) {
    for (const dir of (process.env.PATH || "").split(delimiter)) {
        if (dir && existsSync(join(dir, name))) return join(dir, name);
    }
    return null;
}

export function findXvfb() {
    return WINDOWS ? null : onPath("Xvfb");
}

// Start Xvfb on a free display number; resolves to ":<n>" once its socket exists.
export async function startXvfb(run) {
    for (let n = 90; n < 200; n++) {
        if (existsSync(`/tmp/.X11-unix/X${n}`) || existsSync(`/tmp/.X${n}-lock`)) continue;
        run.spawn(findXvfb(), [`:${n}`, "-screen", "0", "1280x1024x24", "-nolisten", "tcp"]);
        for (let i = 0; i < 100; i++) {
            if (existsSync(`/tmp/.X11-unix/X${n}`)) return `:${n}`;
            await sleep(50);
        }
        throw new Error(`Xvfb :${n} did not start`);
    }
    throw new Error("no free X display number for Xvfb");
}

export function findBrowser(explicit) {
    if (explicit) return explicit;
    for (const name of BROWSERS) {
        const found = onPath(name);
        if (found) return found;
    }
    for (const path of installedBrowsers()) {
        if (existsSync(path)) return path;
    }
    throw new Error(`no Chromium-based browser found (tried ${BROWSERS.join(", ")}, and where they install); ` +
                    "set --browser or WEBCHECK_BROWSER");
}

export function freePort() {
    return new Promise((resolvePort, reject) => {
        const srv = createServer();
        srv.on("error", reject);
        srv.listen(0, "127.0.0.1", () => {
            const { port } = srv.address();
            srv.close(() => resolvePort(port));
        });
    });
}

export async function waitFor(url, what, timeoutMs = 10000) {
    for (let i = 0; i < timeoutMs / 100; i++) {
        try {
            const res = await fetch(url);
            if (res.ok) return res;
        } catch { /* not up yet */ }
        await sleep(100);
    }
    throw new Error(`${what} did not start (${url})`);
}

// Minimal DevTools-protocol session on one page target.
export async function openSession(wsUrl) {
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

// Everything this run starts, and how to stop all of it.
//
// Killing the spawned browser process isn't enough: Chromium-based browsers leave
// helper processes (zygotes, renderers, crashpad) that outlive the launcher. So:
//   - every child is stopped with its descendants: its process group, which it starts
//     in, on Linux and macOS, and its process tree (taskkill /T) on Windows;
//   - every run has a unique profile directory, and any process whose command line
//     names it belongs to this run and is swept up afterwards;
//   - a detached watchdog (tools/webwatch.py) waits for this Node process to disappear
//     (a crash, a kill) and then does the same, so nothing leaks even if Node never gets
//     to run its cleanup. It is harmless when cleanup already ran.
export class RunProcesses {
    constructor(name) {
        this.profile = mkdtempSync(join(tmpdir(), `libwgrender-${name}-`));
        this.pids = [];
        this.stopped = false;
        const watchdog = spawn(PYTHON, [join(ROOT, "tools", "webwatch.py"), String(process.pid), this.profile], {
            detached: true, stdio: "ignore", windowsHide: true,
        });
        watchdog.on("error", () => { /* no Python: the run's own cleanup still stops everything */ });
        watchdog.unref();
    }

    // Start a child (in its own process group, off Windows) and record it for the
    // watchdog. `log`: a file for its output (else it's discarded).
    spawn(command, args, env = process.env, log = null) {
        const out = log ? openSync(log, "w") : "ignore";
        const child = spawn(command, args, { stdio: ["ignore", out, out], detached: !WINDOWS, windowsHide: true, env });
        if (log) closeSync(out);
        child.unref();
        if (child.pid) {
            this.pids.push(child.pid);
            writeFileSync(`${this.profile}.pids`, this.pids.join(" "));
        }
        return child;
    }

    // Processes (other than this one) whose command line names the run's profile.
    strays() {
        let listing = "";
        try {
            listing = WINDOWS
                ? execFileSync("powershell", ["-NoProfile", "-Command",
                    "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -and " +
                    `$_.CommandLine.Contains('${this.profile}') } | ForEach-Object { $_.ProcessId }`],
                    { encoding: "utf8", windowsHide: true })
                : execFileSync("ps", ["-eo", "pid=,args="], { encoding: "utf8" })
                    .split("\n").filter((line) => line.includes(this.profile)).join("\n");
        } catch {
            return [];
        }
        return listing.split("\n")
            .map((line) => Number.parseInt(line.trim(), 10))
            .filter((pid) => Number.isInteger(pid) && pid !== process.pid);
    }

    kill(pid, signal) {
        if (WINDOWS) {
            /* no signals on Windows: the tree, forcibly (a headless browser has no window
             * to be asked to close) */
            try { execFileSync("taskkill", ["/T", "/F", "/PID", String(pid)], { stdio: "ignore", windowsHide: true }); } catch { /* gone */ }
            return;
        }
        try { process.kill(-pid, signal); } catch { /* not a group, or already gone */ }
        try { process.kill(pid, signal); } catch { /* already gone */ }
    }

    signalAll(signal) {
        for (const pid of this.pids) this.kill(pid, signal);
        for (const pid of this.strays()) this.kill(pid, signal);
    }

    removeFiles() {
        rmSync(this.profile, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
        rmSync(`${this.profile}.pids`, { force: true });
        rmSync(`${this.profile}.log`, { force: true });
    }

    // Normal path: ask politely, give the browser a moment, then force.
    async stop() {
        if (this.stopped) return;
        this.stopped = true;
        if (WINDOWS) { // taskkill /F is already the forceful kind
            this.signalAll("SIGKILL");
            this.removeFiles();
            return;
        }
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
        try { this.removeFiles(); } catch { /* the watchdog removes them */ }
    }
}


// Start a browser for a run, with DevTools on a free port; resolves to its base URL
// and a session on the browser. display: "headless" (WebGL2 on SwiftShader, a CPU
// renderer), "xvfb" (the GPU through ANGLE on Vulkan, on a private virtual display
// started here) or "screen".
export async function launchBrowser(run, browserPath, { display, backend, profile = run.profile, windowSize = "1024,900", extraArgs = [] }) {
    let env = process.env;
    if (display === "xvfb") {
        const { WAYLAND_DISPLAY, ...rest } = process.env; // X11 on the virtual display
        run.xDisplay ??= await startXvfb(run);
        env = { ...rest, DISPLAY: run.xDisplay, XDG_SESSION_TYPE: "x11" };
    }
    const debugPort = await freePort();
    run.spawn(browserPath, [
        ...(display === "headless" ? ["--headless=new"] : []),
        `--remote-debugging-port=${debugPort}`,
        `--user-data-dir=${profile}`,
        "--no-first-run",
        "--no-default-browser-check",
        `--window-size=${windowSize}`,
        "--autoplay-policy=no-user-gesture-required",
        /* a fake audio device: examples start audio on load; it still runs, but
         * nothing reaches PipeWire/PulseAudio or the speakers */
        "--disable-audio-output",
        ...(display === "headless" ? ["--use-angle=swiftshader", "--enable-unsafe-swiftshader"] : []),
        ...(display === "xvfb"
            ? ["--ozone-platform=x11", "--enable-unsafe-webgpu", "--enable-features=Vulkan", "--use-angle=vulkan"]
            : []),
        ...extraArgs,
        "about:blank",
    ], env, `${profile}.log`);
    const debugBase = `http://127.0.0.1:${debugPort}`;
    let version;
    try {
        /* a cold start on a busy CI runner can take a while */
        version = await (await waitFor(`${debugBase}/json/version`, "browser", 30000)).json();
    } catch (err) {
        let output = "";
        try { output = readFileSync(`${profile}.log`, "utf8").trim().split("\n").slice(-20).join("\n"); } catch { /* none */ }
        throw new Error(`${err.message}; its last output:\n${output || "(nothing)"}`);
    }
    return { debugBase, browser: await openSession(version.webSocketDebuggerUrl) };
}
