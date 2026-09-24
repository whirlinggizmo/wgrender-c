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
 *     make serve
 *     WGRENDER_ASSET_HOST=http://localhost:8000/assets ./examples/build/desktop/fetch
 *
 * Offline, or built headless for `make smoke` (a gate shouldn't need a network), it
 * reads the local asset directory instead and says so. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "example_assets.h"
#include "wgr.h"

#define TEXTURE_PATH "sprites/logo/wg-logo-white-alpha.png"
#define CACHE_DIR "build/asset-cache"
#define DEFAULT_HOST "https://raw.githubusercontent.com/whirlinggizmo/wgrender-c/main/examples/assets"

static wgr_handle_t g_sprite;
static char g_host[256];

#ifndef __EMSCRIPTEN__ /* the browser downloads by itself */
static bool g_remote;
static int g_downloads;

/* Download `url` to `dest_path`, then say how it went. A real one wouldn't block. */
static void fetch_with_curl(wgr_handle_t request, const char *url, const char *dest_path, void *user)
{
    char command[1024];
    (void)user;
    snprintf(command, sizeof command, "curl -fsS --max-time 30 -o \"%s\" \"%s\"", dest_path, url);
    const bool ok = system(command) == 0;
    if (ok) {
        g_downloads++;
    }
    wgr_asset_fetch_done(request, ok);
}

/* Is anything serving there? Keeps `make smoke` (and a forgetful human) honest. */
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
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("could not get %s", path);
}

static void on_init(void *user)
{
    (void)user;
    (void)wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g_sprite = wgr_sprite2d_create(0);
    wgr_sprite2d_set_position(g_sprite, 512, 380);
    wgr_debug_enable_fps(12, 10, 16);

#ifdef __EMSCRIPTEN__
    snprintf(g_host, sizeof g_host, "%s", EXAMPLE_ASSET_BASE); /* the browser fetches */
#else
    const char *wanted = getenv("WGRENDER_ASSET_HOST");
    snprintf(g_host, sizeof g_host, "%s", wanted != NULL ? wanted : DEFAULT_HOST);
#ifdef WGR_HEADLESS
    g_remote = wanted != NULL && host_is_up(g_host); /* `make smoke` stays offline */
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
    wgr_asset_add_task(wgr_asset_ensure_async(TEXTURE_PATH, NULL, 0), on_loaded, on_failed, NULL);
}

static void frame(float dt, float fraction, void *user)
{
    char line[512];
    (void)dt;
    (void)fraction;
    (void)user;

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(28, 30, 38, 255));
    wgr_sprite2d_draw(g_sprite);
    wgr_text_draw("libwgrender fetch: the desktop build downloads what the browser downloads", 12, 36, 20,
                  WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof line, "host: %s", g_host);
    wgr_text_draw(line, 12, 64, 16, WGR_COLOR_LIGHTGRAY);
#ifndef __EMSCRIPTEN__
    if (g_remote) {
        snprintf(line, sizeof line, "downloaded %d file(s) into %s   (delete it and re-run: they come back)",
                 g_downloads, CACHE_DIR);
    } else {
        snprintf(line, sizeof line, "no host reachable — reading %s locally instead", EXAMPLE_ASSET_BASE);
    }
    wgr_text_draw(line, 12, 86, 16, WGR_COLOR_LIGHTGRAY);
#endif
    wgr_render_end_frame();

    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(1024, 640, "libwgrender fetch", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
