/* libsk force_fetch example — exercises both ensure overrides at once:
 * `fetch_url` (per-call source override) and SK_ASSET_FORCE_FETCH.
 *
 * The asset KEY is a bogus path (nothing exists at host + key, so a plain ensure
 * would just fail), while `fetch_url` points at an explicit source URL and
 * FORCE_FETCH bypasses the cache. On web the bytes are pulled from that URL and
 * cached under the key; on desktop (no network fetcher yet) it falls back to
 * loading the real file locally so the example still plays.
 * Press M to toggle the looping music. */
#include <stddef.h>
#include <string.h>

#include "sk.h"
#include "example_assets.h"

#define MUSIC_PATH "music/ethernight_club.mp3"
#define INVALID_MUSIC_PATH "music/ethernight_club_invalid.mp3" /* intentionally invalid to demonstrate force_fetch */
/* explicit source URL, used verbatim. Root-relative so it works on whatever host
 * and port serves the page (make serve, make webcheck); an absolute
 * https://cdn.example/... URL is passed through the same way. */
#define MUSIC_FORCE_FETCH_PATH "/assets/music/ethernight_club.mp3"

static sk_color_t g_bg;
static sk_handle_t g_music;
static bool g_music_on;

static void on_music_loaded(const char *path, void *user)
{
    sk_handle_t audio = sk_audio_create(path);
    (void)user;
    g_music = sk_sound_create(audio);
    sk_audio_release(audio); /* the sound holds its own reference */
    sk_sound_set_volume(g_music, 0.5f);
    sk_sound_set_loop(g_music, true); /* "music" is just a looping sound */
    sk_sound_play(g_music);
    g_music_on = true;
}

static void on_failed(const char *p, void *u) { (void)u; sk_logger_error("load failed: %s", p); }

static void on_init(void *user_data)
{
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;
    g_bg = sk_color_rgba(18, 20, 28, 255);

    if (strcmp(sk_get_platform(), "web") == 0) {
        /* Web: demonstrate fetch_url + FORCE_FETCH. The key (INVALID_MUSIC_PATH)
         * is a bogus path, so the bytes can only come from the explicit
         * source URL, proving the override is honored and cached under the key. */
        sk_asset_add_task(sk_asset_ensure_async(INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH,
                                                SK_ASSET_FORCE_FETCH),
                          on_music_loaded, on_failed, NULL);
        log_info("force_fetch: %s from %s", INVALID_MUSIC_PATH, MUSIC_FORCE_FETCH_PATH);
    } else {
        /* Desktop has no network fetcher yet, so fetch_url/FORCE_FETCH are no-ops;
         * load the real file from the local asset dir so the example still plays. */
        sk_asset_add_task(sk_asset_ensure_async(MUSIC_PATH, NULL, 0),
                          on_music_loaded, on_failed, NULL);
        log_info("force_fetch is web-only; loading %s locally on desktop", MUSIC_PATH);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    sk_keyboard_state_t kb = sk_input_get_keyboard_state();

    if (kb.keys[SK_KEY_M] == SK_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { sk_sound_pause(g_music); } else { sk_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_text_draw("libsk + sokol_audio + force_fetch", 24, 30, 28, SK_COLOR_RAYWHITE);
    sk_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, SK_COLOR_SKYBLUE);
    sk_text_draw("[M] toggle music   [ESC] quit",
                 24, 150, 16, SK_COLOR_LIGHTGRAY);

    sk_text_draw_fps(24, 12);
    sk_render_end();

    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_init_values(720, 240, "libsk audio + force_fetch", 0);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
