// Types for src/guest.js: the guest side of the guest ABI (bindings/host/wgr_guest.h).

/** The ops a JS program gives wgrender. All optional. */
export interface Ops {
    /** Once, before the first frame. */
    init?: (() => void) | null;
    /** Every frame: `dt` in seconds, `frameId` counting from 1. */
    frame?: ((dt: number, frameId: number) => void) | null;
    /** Once, after the last frame. */
    shutdown?: (() => void) | null;
}

/** Declare the ops; a later call replaces all three. */
export declare function register(ops?: Ops): void;
/** Fixed-rate simulation: `tick(dt)` 0..N times before each frame, dt = 1/hz; hz <= 0 turns it off. */
export declare function registerTick(tick: (dt: number) => void, hz: number): void;
/** How far this frame is into the next tick, 0..1; 0 without a tick. */
export declare function tickFraction(): number;
/** 0: log a fault and keep calling (the default); 1: log it, stop calling, quit. */
export declare function setFaultPolicy(policy: 0 | 1): void;
/** True once an op has faulted. */
export declare function faulted(): boolean;
/**
 * Attach the host (what createWgrHost() resolved to), check it is the wgrender this binding
 * was generated from, and run. False, with the reason logged, on a major or minor mismatch.
 */
export declare function start(host: unknown, width: number, height: number, title: string, flags?: number): boolean;
