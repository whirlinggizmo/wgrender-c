/* libwgrender force_fetch example — exercises both ensure overrides at once:
 * `fetch_url` (per-call source override) and WGR_ASSET_FORCE_FETCH.
 *
 * The asset KEY is a bogus path (nothing exists at host + key, so a plain ensure
 * would just fail), while `fetch_url` names where the bytes really are and
 * FORCE_FETCH bypasses the cache. It is relative, so it is read against the host as
 * a browser reads a URL against a directory -- the same call on every platform. On
 * web the bytes are downloaded and cached under the key; on desktop, whose host is a
 * local directory, the file is read where it is, under the key's name. With a URL
 * host and a fetcher, desktop would download it as the web does.
 * Press M to toggle the looping music. */
#include <stddef.h>

#include "wgr.h"
#include "shared/example_assets.h"

#define INVALID_MUSIC_PATH "music/invalid.mp3" /* intentionally invalid to demonstrate force_fetch */
/* where the bytes are, relative to the asset host: under it wherever the site is
 * served, GitHub Pages' /<repo>/ included. An absolute https://cdn.example/... URL is
 * used as it is. */
#define MUSIC_FORCE_FETCH_PATH MUSIC_PATH

static wgr_color_t g_bg;
static wgr_handle_t g_music;
static bool g_music_on;

static void on_music_loaded(const char *path, void *user)
{
    wgr_handle_t audio = wgr_audio_create(path);
    (void)user;
    g_music = wgr_sound_create(audio);
    wgr_audio_release(audio); /* the sound holds its own reference */
    wgr_sound_set_volume(g_music, 0.5f);
    wgr_sound_set_loop(g_music, true); /* "music" is just a looping sound */
    wgr_sound_play(g_music);
    g_music_on = true;
}

static void on_failed(const char *p, void *u) { (void)u; wgr_logger_error("load failed: %s", p); }

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    (void)user_data;
    g_bg = wgr_color_rgba(18, 20, 28, 255);

    /* The key (INVALID_MUSIC_PATH) is a bogus path, so the bytes can only come from
     * the explicit source, proving the override is honored. */
    wgr_asset_add_task(wgr_asset_ensure_async(INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH, WGR_ASSET_FORCE_FETCH),
                       on_music_loaded, on_failed, NULL);
    log_info("force_fetch: %s from %s", INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();

    if (kb.keys[WGR_KEY_M] == WGR_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { wgr_sound_pause(g_music); } else { wgr_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    wgr_text_draw("libwgrender + sokol_audio + force_fetch", 24, 30, 28, WGR_COLOR_RAYWHITE);
    wgr_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, WGR_COLOR_SKYBLUE);
#ifndef __EMSCRIPTEN__ /* the quit key's hint, as the key: desktop only */
    wgr_text_draw("[M] toggle music   [ESC] quit",
                 24, 150, 16, WGR_COLOR_LIGHTGRAY);
#else
    wgr_text_draw("[M] toggle music",
                 24, 150, 16, WGR_COLOR_LIGHTGRAY);
#endif

    wgr_text_draw_fps(24, 12);
    wgr_render_end_frame();

#ifndef __EMSCRIPTEN__ /* on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to */
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
#endif
}

int main(void)
{
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_init_values(720, 240, "libwgrender audio + force_fetch", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
