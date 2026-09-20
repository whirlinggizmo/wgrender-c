/* libwgrender force_fetch example — exercises both ensure overrides at once:
 * `fetch_url` (per-call source override) and WGR_ASSET_FORCE_FETCH.
 *
 * The asset KEY is a bogus path (nothing exists at host + key, so a plain ensure
 * would just fail), while `fetch_url` points at an explicit source URL and
 * FORCE_FETCH bypasses the cache. On web the bytes are pulled from that URL and
 * cached under the key; on desktop (no network fetcher yet) it falls back to
 * loading the real file locally so the example still plays.
 * Press M to toggle the looping music. */
#include <stddef.h>
#include <string.h>

#include "wgr.h"
#include "example_assets.h"

#define MUSIC_PATH "music/ethernight_club.mp3"
#define INVALID_MUSIC_PATH "music/ethernight_club_invalid.mp3" /* intentionally invalid to demonstrate force_fetch */
/* explicit source URL, used verbatim. Relative to the page, so it works on whatever
 * host serves the site and at whatever depth -- "/assets/..." would be the server root,
 * which is wrong wherever the site isn't at one (GitHub Pages serves a project under
 * /<repo>/). An absolute https://cdn.example/... URL is passed through the same way. */
#define MUSIC_FORCE_FETCH_PATH "assets/music/ethernight_club.mp3"

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
    (void)user_data;
    g_bg = wgr_color_rgba(18, 20, 28, 255);

    if (strcmp(wgr_get_platform(), "web") == 0) {
        /* Web: demonstrate fetch_url + FORCE_FETCH. The key (INVALID_MUSIC_PATH)
         * is a bogus path, so the bytes can only come from the explicit
         * source URL, proving the override is honored and cached under the key. */
        wgr_asset_add_task(wgr_asset_ensure_async(INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH,
                                                WGR_ASSET_FORCE_FETCH),
                          on_music_loaded, on_failed, NULL);
        log_info("force_fetch: %s from %s", INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH);
    } else {
        /* Desktop has no network fetcher yet, so fetch_url/FORCE_FETCH are no-ops;
         * load the real file from the local asset dir so the example still plays. */
        wgr_asset_add_task(wgr_asset_ensure_async(MUSIC_PATH, NULL, 0),
                          on_music_loaded, on_failed, NULL);
        log_info("force_fetch is web-only; loading %s locally on desktop", MUSIC_PATH);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();

    if (kb.keys[WGR_KEY_M] == WGR_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { wgr_sound_pause(g_music); } else { wgr_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    wgr_render_begin();
    wgr_render_clear_background(g_bg);

    wgr_text_draw("libwgrender + sokol_audio + force_fetch", 24, 30, 28, WGR_COLOR_RAYWHITE);
    wgr_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, WGR_COLOR_SKYBLUE);
    wgr_text_draw("[M] toggle music   [ESC] quit",
                 24, 150, 16, WGR_COLOR_LIGHTGRAY);

    wgr_text_draw_fps(24, 12);
    wgr_render_end();

    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_init_values(720, 240, "libwgrender audio + force_fetch", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
