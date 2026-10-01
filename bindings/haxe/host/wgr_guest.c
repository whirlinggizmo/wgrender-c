/* Host glue for the guest ABI — see wgr_guest.h.
 *
 * Analogous to wg-vf's native/wg_vf.c: the glue owns the exports the host calls and
 * the bookkeeping, so a guest only implements handlers. Here the "host" calling in is
 * JavaScript, and the ops it installs are JS functions made into C function pointers
 * with Emscripten's addFunction. */
#include <stdint.h>
#include <stddef.h>

#include "wgr.h"
#include "wgr_guest.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define GUEST_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define GUEST_EXPORT
#endif

static wgr_guest_init_fn guest_init;
static wgr_guest_frame_fn guest_frame;
static wgr_guest_asset_fn guest_asset;
static wgr_guest_shutdown_fn guest_shutdown;
static wgr_guest_tick_fn guest_tick;
static int guest_tick_hz;

/* The ops, as wgrender's lifecycle callbacks; defined below, named here because
 * wgr_guest_register_tick and wgr_guest_install both install them from above. */
static void host_init(void *user);
static void host_tick(float dt, void *user);
static void host_frame(float dt, float tick_fraction, void *user);
static void host_shutdown(void *user);

static int fault_policy = WGR_GUEST_FAULT_CONTINUE;
static int faulted;
static int installed;
static uint32_t frame_id;
static float last_tick_fraction;

GUEST_EXPORT void wgr_guest_register(wgr_guest_init_fn init, wgr_guest_frame_fn frame,
                                     wgr_guest_asset_fn asset, wgr_guest_shutdown_fn shutdown)
{
    guest_init = init;
    guest_frame = frame;
    guest_asset = asset;
    guest_shutdown = shutdown;
}

GUEST_EXPORT void wgr_guest_set_fault_policy(int policy) { fault_policy = policy; }
GUEST_EXPORT uint32_t wgr_guest_frame_id(void) { return frame_id; }
GUEST_EXPORT float wgr_guest_tick_fraction(void) { return last_tick_fraction; }

GUEST_EXPORT void wgr_guest_register_tick(wgr_guest_tick_fn tick, int hz)
{
    guest_tick = tick;
    guest_tick_hz = hz;
    /* Registering a tick after the ops are in place has to reach wgrender now;
     * wgr_guest_install is what would have done it, and it has already run. */
    if (installed && tick != NULL && hz > 0) {
        wgr_set_tick(host_tick, NULL, hz);
    }
}
GUEST_EXPORT int wgr_guest_faulted(void) { return faulted; }

static void fault(const char *op, int code)
{
    wgr_logger_message(WGR_LOGGER_LEVEL_ERROR, "wgr_guest: %s faulted (%d)", op, code);
    if (fault_policy == WGR_GUEST_FAULT_FATAL) {
        faulted = 1;
        wgr_request_quit();
    }
}

/* --- the ops, as wgrender's lifecycle callbacks --- */

static void host_init(void *user)
{
    (void)user;
    if (guest_init == NULL) {
        return;
    }
    int rc = guest_init();
    if (rc != 0) {
        fault("init", rc);
    }
}

static void host_tick(float dt, void *user)
{
    (void)user;
    if (faulted || guest_tick == NULL) {
        return;
    }
    int rc = guest_tick(dt);
    if (rc != 0) {
        fault("tick", rc);
    }
}

static void host_frame(float dt, float tick_fraction, void *user)
{
    (void)user;
    last_tick_fraction = tick_fraction;
    frame_id++;
    if (faulted || guest_frame == NULL) {
        return;
    }
    int rc = guest_frame(dt, frame_id);
    if (rc != 0) {
        fault("frame", rc);
    }
}

/* The last op to run, once, on the way out. It was stored by wgr_guest_register from
 * the beginning and never called: nothing here registered a cleanup with wgrender, so
 * the slot existed and no guest could observe it. Porting examples/quit.c, whose whole
 * subject is what happens after the last frame, is what noticed.
 *
 * It runs even when the guest has faulted. A faulted guest is the case where releasing
 * whatever it holds outside wgrender matters most, and by here there are no more frames
 * for a second fault to spoil. */
static void host_shutdown(void *user)
{
    (void)user;
    if (guest_shutdown == NULL) {
        return;
    }
    int rc = guest_shutdown();
    if (rc != 0) {
        /* Not fault(): that can call wgr_request_quit, and wgrender is already
         * quitting. Say it happened and let the teardown finish. */
        wgr_logger_message(WGR_LOGGER_LEVEL_ERROR, "wgr_guest: shutdown faulted (%d)", rc);
    }
}

/* --- assets: the glue holds the function pointers, the guest holds an id --- */

static void asset_done(const char *path, void *user, int ok)
{
    if (faulted || guest_asset == NULL) {
        return;
    }
    int rc = guest_asset((uint32_t)(uintptr_t)user, path, ok);
    if (rc != 0) {
        fault("asset", rc);
    }
}

static void asset_ok(const char *path, void *user) { asset_done(path, user, 1); }
static void asset_failed(const char *path, void *user) { asset_done(path, user, 0); }

GUEST_EXPORT int wgr_guest_asset_load(const char *path, uint32_t id, const char *fetch_url, uint32_t flags)
{
    wgr_handle_t task = wgr_asset_ensure_async(path, fetch_url, flags);
    return wgr_asset_add_task(task, asset_ok, asset_failed, (void *)(uintptr_t)id)
           == WGR_ASSET_ADD_TASK_OK;
}

/* Put the ops in wgrender's lifecycle slots, without opening a window or running.
 *
 * wgr_guest_start does this on its way to wgr_run, which is all a guest needs. A
 * program driving wgrender itself -- wgr_init_values, its own callbacks, wgr_run --
 * needs the slots filled without the rest, because host_init and friends are static
 * here and it cannot reach them.
 *
 * Unconditional on purpose, and safe to call repeatedly. wgr_init_values memsets
 * wgrender's runtime, so anything installed before it is erased: a program that sets
 * a handler, then starts, has to have the slots filled again afterwards. Setting the
 * same three function pointers twice costs nothing; skipping the second one costs the
 * whole lifecycle, silently, which is how this was found. */
GUEST_EXPORT void wgr_guest_install(void)
{
    installed = 1;
    wgr_set_init(host_init, NULL);
    wgr_set_frame(host_frame, NULL);
    wgr_set_cleanup(host_shutdown, NULL);
    if (guest_tick != NULL && guest_tick_hz > 0) {
        wgr_set_tick(host_tick, NULL, guest_tick_hz);
    }
}

GUEST_EXPORT int wgr_guest_start(int width, int height, const char *title, uint32_t flags)
{
    /* Copied: a guest passing scratch memory shouldn't have to keep it alive. */
    static char kept_title[128];
    size_t i = 0;
    if (title != NULL) {
        for (; i + 1 < sizeof kept_title && title[i] != '\0'; i++) {
            kept_title[i] = title[i];
        }
    }
    kept_title[i] = '\0';

    wgr_init_values(width, height, kept_title, flags);
    wgr_guest_install();
    return wgr_run();
}

/* Who calls main() is where the two platforms genuinely differ.
 *
 * On the web the guest is JS, so nothing in this module can be the entry point:
 * Emscripten wants one anyway, and the page's boot script is what actually registers
 * the guest and calls wgr_guest_start.
 *
 * On desktop the guest is compiled in, and its own runtime supplies main() — hxcpp's
 * here. Defining one as well is a duplicate symbol at link time.
 *
 * The same is true of an all-in-one *web* build: hxcpp through emcc produces one wasm
 * with the Haxe program's main in it, and this glue linked alongside. That build
 * defines WGR_GUEST_NO_MAIN to say so, because C cannot tell the two web shapes
 * apart from in here. */
#if defined(__EMSCRIPTEN__) && !defined(WGR_GUEST_NO_MAIN)
int main(void) { return 0; }
#endif
