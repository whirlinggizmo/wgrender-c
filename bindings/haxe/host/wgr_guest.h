/* The guest ABI: wgrender as a host, a guest module as the game.
 *
 * libwgrender owns the window, the loop, assets, input and the scene; the guest owns
 * game state and logic. This is wg-vf's host/guest split applied to a renderer — see
 * wg-vf's docs/vignette-author-guide.md, whose operation table this mirrors.
 *
 * The guest registers four ops and the host calls them. Guarantees the host makes,
 * the same ones wg-vf promises:
 *
 *   1. Serial, non-reentrant, single-threaded. Ops are never called concurrently or
 *      nested. The guest MAY call wgrender's API during an op — that is the point —
 *      but the host will not re-enter the guest while one is running.
 *   2. `init` first, `shutdown` last. Nothing before init returns, nothing after
 *      shutdown begins.
 *   3. `asset` fires on the main thread during a later frame, never inside another op.
 *
 * Ops return 0 for ok and nonzero for a fault. A guest in a language with exceptions
 * catches at its own edge and converts a throw into a nonzero return: an exception
 * that escapes into these C frames takes the loop down (measured: on the web the page
 * freezes with an opaque `Uncaught [object WebAssembly.Exception]`).
 *
 * What the host does with a fault is policy — see wgr_guest_fault_policy_t.
 */
#ifndef WGR_GUEST_H
#define WGR_GUEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*wgr_guest_init_fn)(void);
/* dt in seconds; frame_id counts rendered frames from 1 and wraps at 2^32. */
typedef int (*wgr_guest_frame_fn)(float dt, uint32_t frame_id);
/* `id` is the guest's own key from wgr_guest_asset_load; ok is 1 or 0. */
typedef int (*wgr_guest_asset_fn)(uint32_t id, const char *path, int ok);
typedef int (*wgr_guest_shutdown_fn)(void);
/* Fixed-rate simulation, 0..N times before each frame, always with dt = 1/hz.
 * Optional, and the only op that needs configuring, which is why it is registered on
 * its own rather than passed to wgr_guest_register with the other four. */
typedef int (*wgr_guest_tick_fn)(float dt);

typedef enum {
    /* Log the fault and keep calling the guest: a bad frame stays a bad frame. */
    WGR_GUEST_FAULT_CONTINUE = 0,
    /* Log it, stop calling the guest, and ask the host to quit. wg-vf's rule for a
     * host-driven op, on the grounds that a faulted guest's state is untrustworthy. */
    WGR_GUEST_FAULT_FATAL = 1,
} wgr_guest_fault_policy_t;

void wgr_guest_register(wgr_guest_init_fn init, wgr_guest_frame_fn frame,
                        wgr_guest_asset_fn asset, wgr_guest_shutdown_fn shutdown);
void wgr_guest_set_fault_policy(int policy); /* default: CONTINUE */

/* Register the tick and its rate. hz <= 0 or a NULL fn means no tick, as
 * wgr_set_tick has it. Call before wgr_guest_start; it takes effect there. */
void wgr_guest_register_tick(wgr_guest_tick_fn tick, int hz);

/* Put the ops in wgrender's lifecycle slots without opening a window or running, for
 * a program that drives wgrender itself (wgr_init_values, then wgr_run) and still
 * wants its handlers called. wgr_guest_start does this for a guest. Idempotent. */
void wgr_guest_install(void);

/* Open the window and run. Returns when the loop ends — at once on the web, where
 * the browser drives frames from here on. Register before calling this. */
int wgr_guest_start(int width, int height, const char *title, uint32_t flags);

/* Make `path` local, then call the guest's asset op with `id`. The guest never sees
 * a function pointer. False if the task could not be queued (no callback follows).
 *
 * `fetch_url` and `flags` are wgr_asset_ensure_async's, passed through: NULL and 0
 * are the plain "fetch it from the host under this key" this took before. A
 * fetch_url downloads from exactly that URL while still caching and resolving under
 * `path`, and WGR_ASSET_FORCE_FETCH ignores what is already cached. */
int wgr_guest_asset_load(const char *path, uint32_t id, const char *fetch_url, uint32_t flags);

/* Introspection, for tests and for a guest that wants to know. */
uint32_t wgr_guest_frame_id(void);
/* How far the current frame is into the next tick, 0..1, for drawing tick state
 * smoothly: lerp(previous, current, fraction). 0 when there is no tick. Only
 * meaningful inside the frame op. */
float wgr_guest_tick_fraction(void);
int wgr_guest_faulted(void);

#ifdef __cplusplus
}
#endif

#endif /* WGR_GUEST_H */
