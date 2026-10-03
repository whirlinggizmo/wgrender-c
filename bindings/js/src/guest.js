// The guest side of bindings/host/wgr_guest.h for a JS program: wgrender is the host,
// an Emscripten module, and the program's ops are JS functions it calls.
// Hand-written; the Haxe binding's GuestAbi.js.hx is the same contract.
//
// Each op wrapper owns two things the C side can't do for the program:
//
// - it catches, turning a throw into the nonzero return the ABI expects, because an
//   exception escaping into the host's frames freezes the page on an opaque
//   "Uncaught [object WebAssembly.Exception]";
// - it releases the scratch arena (runtime.js) once when the op ends, fault or not.

import { host, attach, cstr } from './runtime.js';
import { BUILT_VERSION } from '../wgrender.js';

let onInit = null;
let onFrame = null;
let onTick = null;
let onShutdown = null;
let installed = false;
let attached = false; // start() attaches; ops registered before it are installed there

function op(name, signature, body) {
    return host.addFunction((...args) => {
        const mark = host.stackSave();
        let code = 0;
        try {
            body(...args);
        } catch (e) {
            console.error(`wgrender guest: uncaught exception in ${name}:`, e);
            code = 1;
        }
        host.stackRestore(mark);
        return code;
    }, signature);
}

// The dispatchers wgrender holds read the handlers when they fire, so a handler can
// change without a second function entering the wasm table (which can't take one back).
function install() {
    if (installed) return;
    installed = true;
    host._wgr_guest_register(
        op('init', 'i', () => onInit && onInit()),
        op('frame', 'ifi', (dt, frameId) => onFrame && onFrame(dt, frameId >>> 0)),
        op('shutdown', 'i', () => onShutdown && onShutdown()));
    host._wgr_guest_install();
}

/**
 * Declare the ops. `frame(dt, frameId)` runs every frame; `init()` once before the first,
 * `shutdown()` once after the last. All optional; a later call replaces all three.
 */
export function register({ init = null, frame = null, shutdown = null } = {}) {
    onInit = init;
    onFrame = frame;
    onShutdown = shutdown;
    if (attached) install();
}

/**
 * Fixed-rate simulation: `tick(dt)` runs 0..N times before each frame, always with dt of
 * 1/hz. Draw in the frame op, using tickFraction() to interpolate. hz <= 0 turns it off.
 */
let tickHz = 0;

export function registerTick(tick, hz) {
    onTick = tick;
    tickHz = hz;
    if (attached) installTick();
}

function installTick() {
    host._wgr_guest_register_tick(op('tick', 'if', (dt) => onTick && onTick(dt)), tickHz);
}

/** How far this frame is into the next tick, 0..1; 0 without a tick. */
export function tickFraction() {
    return host._wgr_guest_tick_fraction();
}

/** What the host does when an op throws: WGR_GUEST_FAULT_CONTINUE (0, the default) logs
 * and keeps calling; WGR_GUEST_FAULT_FATAL (1) logs, stops calling, and quits. */
export function setFaultPolicy(policy) {
    host._wgr_guest_set_fault_policy(policy);
}

/** True once an op has faulted. */
export function faulted() {
    return host._wgr_guest_faulted() !== 0;
}

/**
 * Attach the host, check it is the wgrender this binding was generated from, and run.
 * Refuses (returns false, and says why) on a different major or minor version: every
 * call may then be wrong, so starting would turn one clear error into many confusing
 * ones. A patch difference only warns. Ops registered before this are installed here;
 * after it, register() installs them at once.
 */
export function start(module, width, height, title, flags = 0) {
    attach(module);
    const running = [module._wgr_version_major(), module._wgr_version_minor(), module._wgr_version_patch()];
    const built = [BUILT_VERSION.major, BUILT_VERSION.minor, BUILT_VERSION.patch];
    if (running[0] !== built[0] || running[1] !== built[1]) {
        console.error(`wgrender: the host is ${running.join('.')}, but this binding was generated from `
            + `${built.join('.')}; regenerate it (bindings/js/tools/gen_binding.py) or rebuild the host.`);
        return false;
    }
    if (running[2] !== built[2]) {
        console.warn(`wgrender: the host is ${running.join('.')}, the binding ${built.join('.')}`);
    }
    attached = true;
    install();
    if (onTick) installTick();
    const mark = module.stackSave();
    module._wgr_guest_start(width, height, cstr(title), flags);
    module.stackRestore(mark);
    return true;
}
