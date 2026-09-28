/* libwgrender fetch example — desktop pulls its assets from the same site the browser
 * version does.
 *
 * On the web the browser downloads a missing asset and caches it. On desktop
 * libwgrender ships no HTTP client (and no TLS), so it asks the program for one: set a
 * URL as the asset host, hand it a fetcher, and a cache miss becomes a download.
 *
 *     wgr_asset_set_cache_dir("build/asset-cache");
 *     wgr_asset_set_host("http://localhost:8000/assets");
 *     wgr_asset_set_fetcher(fetch_with_curl, NULL);
 *
 * The fetcher here shells out to curl, so the example needs nothing built or linked.
 * It is synchronous, which is fine for a handful of small files but would hitch a frame
 * on a big one; the hook is built for the other way round — a real fetcher (wgutils'
 * fetch_url, WinHTTP, NSURLSession) starts a download and calls wgr_asset_fetch_done
 * from a later tick, and nothing blocks meanwhile.
 *
 * Bytes never cross the boundary: libwgrender names a URL and a destination file, the
 * fetcher writes that file. Downloads land in the cache directory and the next run
 * finds them there, which is the job the browser's cache does on web.
 *
 * It downloads from the project's own assets on GitHub, over HTTPS, so there is nothing
 * to start first — and nothing in libwgrender did the TLS. Point it somewhere else with
 * WGRENDER_ASSET_HOST, e.g. the dev server the web build uses:
 *
 *     python3 tools/serve.py
 *     WGRENDER_ASSET_HOST=http://localhost:8000/assets out/linux/release/fetch
 *
 * Offline, or built headless for the smoke test (a gate shouldn't need a network), it
 * reads the local asset directory instead and says so.
 *
 * Two buttons (examples/shared/ui/ui_widgets.h) show the cache at work: Fetch asset
 * ensures the logo again, and Clear cache forgets what was downloaded, so the next
 * fetch downloads it again. The line under them says what happened -- on desktop,
 * whether the file came from the cache or was downloaded, which the example tells by
 * looking in the cache directory first (wgr_asset_get_cache_dir). The web keeps its
 * cache in the browser, where the example can't look, so there it just says loaded.
 *
 *   ESC  quit */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shared/example_assets.h"
#include "shared/ui/ui_widgets.h"
#include "wgr.h"

#define TEXTURE_PATH "sprites/logo/wg-logo-white-alpha.png"
#ifndef CACHE_DIR /* CMake's desktop build names one in its work directory, build/<platform>/<variant>/ */
#define CACHE_DIR "build/asset-cache"
#endif
#define DEFAULT_HOST "https://raw.githubusercontent.com/whirlinggizmo/wgrender-c/main/examples/assets"

enum { LAYER_CONTROL = 1 }; /* a button's label goes on the layer above (ui_widgets.h) */

static wgr_handle_t g_sprite, g_scene;
static char g_host[256];
static char g_state[512] = "";
static ui_theme_t g_theme;
static ui_button_t g_fetch, g_clear;

#ifndef __EMSCRIPTEN__ /* the browser downloads by itself */
static bool g_remote;
static bool g_was_cached; /* the file was in the cache before this fetch */

/* Download `url` to `dest_path`, then say how it went. A real one wouldn't block. */
static void fetch_with_curl(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    char command[1024];
    (void)user;
    snprintf(command, sizeof command, "curl -fsS --max-time 30 -o \"%s\" \"%s\"", dest_path, url);
    wgr_asset_fetch_done(request, system(command) == 0);
}

/* Is anything serving there? Keeps the smoke test (and a forgetful human) honest. */
static bool host_is_up(const char *host)
{
    char command[512];
    snprintf(command, sizeof command, "curl -fsS -I --max-time 2 -o /dev/null \"%s/%s\"", host, TEXTURE_PATH);
    return system(command) == 0;
}
#endif

static void on_loaded(const char *path, void *user)
{
    const wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_sprite2d_set_texture(g_sprite, texture);
    wgr_texture_release(texture); /* the sprite holds its own reference */
#ifdef __EMSCRIPTEN__
    snprintf(g_state, sizeof g_state, "loaded %s", TEXTURE_PATH);
#else
    if (!g_remote) {
        snprintf(g_state, sizeof g_state, "read %s from %s", TEXTURE_PATH, EXAMPLE_ASSET_BASE);
    } else {
        snprintf(g_state, sizeof g_state, g_was_cached ? "loaded %s from the cache" : "downloaded %s into the cache",
                 TEXTURE_PATH);
    }
#endif
    ui_button_set_enabled(&g_fetch, true);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("could not get %s", path);
    snprintf(g_state, sizeof g_state, "failed to get %s", TEXTURE_PATH);
    ui_button_set_enabled(&g_fetch, true);
}

/* Ensure the logo, having noted whether the cache has it already. */
static void fetch(void)
{
#ifndef __EMSCRIPTEN__
    g_was_cached = false;
    if (g_remote) {
        char cached[1024];
        FILE *f;
        snprintf(cached, sizeof cached, "%s/%s", wgr_asset_get_cache_dir(), TEXTURE_PATH);
        f = fopen(cached, "rb");
        g_was_cached = f != NULL;
        if (f != NULL) fclose(f);
    }
#endif
    wgr_sprite2d_set_texture(g_sprite, 0); /* so a fetch is seen to happen */
    ui_button_set_enabled(&g_fetch, false);
    snprintf(g_state, sizeof g_state, "fetching %s...", TEXTURE_PATH);
    if (wgr_asset_add_task(wgr_asset_ensure_async(TEXTURE_PATH, NULL, 0), on_loaded, on_failed, NULL) !=
        WGR_ASSET_ADD_TASK_OK) {
        snprintf(g_state, sizeof g_state, "couldn't queue %s", TEXTURE_PATH);
        ui_button_set_enabled(&g_fetch, true);
    }
}

static void on_init(void *user)
{
    (void)user;
    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g_sprite = wgr_sprite2d_create(0);
    wgr_sprite2d_set_position(g_sprite, 512, 380);
    wgr_debug_enable_fps(12, 10, 16);

    g_theme = ui_theme_default();
    g_scene = wgr_scene_create();
    wgr_scene_set_active_camera(g_scene, camera);
    wgr_scene_set_interactive(g_scene, true);
    g_fetch = ui_button_create(g_scene, LAYER_CONTROL, "Fetch asset", 12, 150, 180, 40, 17);
    g_clear = ui_button_create(g_scene, LAYER_CONTROL, "Clear cache", 204, 150, 180, 40, 17);

#ifdef __EMSCRIPTEN__
    snprintf(g_host, sizeof g_host, "%s", EXAMPLE_ASSET_BASE); /* the browser fetches */
#else
    const char *wanted = getenv("WGRENDER_ASSET_HOST");
    snprintf(g_host, sizeof g_host, "%s", wanted != NULL ? wanted : DEFAULT_HOST);
#ifdef WGR_HEADLESS
    g_remote = wanted != NULL && host_is_up(g_host); /* the smoke test stays offline */
#else
    g_remote = host_is_up(g_host);
#endif
    if (g_remote) {
        wgr_asset_set_cache_dir(CACHE_DIR);
        wgr_asset_set_fetcher(fetch_with_curl, NULL);
    } else {
        snprintf(g_host, sizeof g_host, "%s", EXAMPLE_ASSET_BASE); /* local directory */
    }
#endif
    wgr_asset_set_host(g_host);
    fetch();
}

static void frame(float dt, float fraction, void *user)
{
    char line[512];
    (void)dt;
    (void)fraction;
    (void)user;

    if (ui_button_update(&g_fetch, g_scene, &g_theme)) {
        fetch();
    }
    if (ui_button_update(&g_clear, g_scene, &g_theme)) {
        wgr_asset_clear_cache();
        snprintf(g_state, sizeof g_state, "cache cleared: the next fetch downloads");
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(28, 30, 38, 255));
    wgr_sprite2d_draw(g_sprite);
    wgr_scene_draw(g_scene);
    wgr_text_draw("libwgrender fetch: the desktop build downloads what the browser downloads", 12, 36, 20,
                  WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof line, "host: %s", g_host);
    wgr_text_draw(line, 12, 64, 16, WGR_COLOR_LIGHTGRAY);
#ifndef __EMSCRIPTEN__
    if (g_remote) {
        snprintf(line, sizeof line, "cache: %s", wgr_asset_get_cache_dir());
    } else {
        snprintf(line, sizeof line, "no host reachable — reading %s locally instead", EXAMPLE_ASSET_BASE);
    }
    wgr_text_draw(line, 12, 86, 16, WGR_COLOR_LIGHTGRAY);
#endif
    wgr_text_draw(g_state, 12, 120, 18, WGR_COLOR_SKYBLUE);
    wgr_render_end_frame();

#ifndef __EMSCRIPTEN__ /* on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to */
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
#endif
}

int main(void)
{
    wgr_init_values(1024, 640, "libwgrender fetch", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
