// Shared by the web tools (webcheck.mjs, webstart.mjs): finding and launching a
// Chromium-based browser (headless, on a virtual X display, or on the screen), a
// minimal DevTools-protocol session, and a record of every process a run starts so
// all of it is stopped, whatever happens to the run.
import { execFileSync, spawn } from "node:child_process";
import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const BROWSERS = ["brave-browser-stable", "google-chrome-stable", "google-chrome", "chromium", "chromium-browser"];

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

export function findXvfb() {
    try {
        return execFileSync("sh", ["-c", "command -v Xvfb"], { encoding: "utf8" }).trim() || null;
    } catch {
        return null;
    }
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
        for (const dir of (process.env.PATH || "").split(":")) {
            if (dir && existsSync(join(dir, name))) return join(dir, name);
        }
    }
    throw new Error(`no Chromium-based browser found (tried ${BROWSERS.join(", ")}); set --browser or WEBCHECK_BROWSER`);
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

export async function waitFor(url, what) {
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
//   - children start in their own process groups, and whole groups are stopped;
//   - every run has a unique profile directory, and any process whose command line
//     names it belongs to this run and is swept up afterwards;
//   - a detached watchdog shell waits for this Node process to disappear (a crash,
//     `kill -9`) and then does the same, so nothing leaks even if Node never gets
//     to run its cleanup. It is harmless when cleanup already ran.
export class RunProcesses {
    constructor(name) {
        this.profile = mkdtempSync(join(tmpdir(), `libsk-${name}-`));
        this.groups = [];
        this.stopped = false;
        const watchdog = spawn("sh", ["-c", `
            while kill -0 "$LIBSK_WEB_NODE_PID" 2>/dev/null; do sleep 1; done
            if [ -f "$LIBSK_WEB_PROFILE.groups" ]; then
                for g in $(cat "$LIBSK_WEB_PROFILE.groups"); do kill -9 "-$g" 2>/dev/null; done  # no "--": dash rejects it
            fi
            ps -eo pid=,comm=,args= | awk -v m="$LIBSK_WEB_PROFILE" '$2 != "sh" && $2 != "awk" && index($0, m) { print $1 }' |
                xargs -r kill -KILL 2>/dev/null
            rm -rf "$LIBSK_WEB_PROFILE" "$LIBSK_WEB_PROFILE.groups"
        `], {
            detached: true,
            stdio: "ignore",
            env: { ...process.env, LIBSK_WEB_NODE_PID: String(process.pid), LIBSK_WEB_PROFILE: this.profile },
        });
        watchdog.unref();
    }

    // Start a child in its own process group and record the group for the watchdog.
    spawn(command, args, env = process.env) {
        const child = spawn(command, args, { stdio: "ignore", detached: true, env });
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
    ], env);
    const debugBase = `http://127.0.0.1:${debugPort}`;
    const version = await (await waitFor(`${debugBase}/json/version`, "browser")).json();
    return { debugBase, browser: await openSession(version.webSocketDebuggerUrl) };
}
