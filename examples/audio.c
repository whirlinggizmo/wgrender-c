/* libsk audio example — looping mp3 music + a one-shot ogg sound.
 *
 * Each file is ensured local (async), then sk_audio_create(path) makes a shared
 * Audio resource: the 6 MB music is streamed (decoded while playing), the small
 * click is decoded up front. Sound objects play them. Mixing runs on the audio
 * device's thread, so music keeps playing through a slow frame: press S to stall
 * one frame for 300 ms. Press SPACE to play the click, M to toggle music. */
#include <stddef.h>

#include "sk.h"
#include "example_assets.h"

#define MUSIC_PATH "music/ethernight_club.mp3"
#define CLICK_PATH "sounds/click_004.ogg"

static sk_color_t g_bg;
static sk_handle_t g_music;
static sk_handle_t g_click;
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

static void on_click_loaded(const char *path, void *user)
{
    sk_handle_t audio = sk_audio_create(path);
    (void)user;
    g_click = sk_sound_create(audio);
    sk_audio_release(audio); /* the sound object holds its own reference */
    sk_sound_set_volume(g_click, 1.0f);
}

static void on_failed(const char *p, void *u) { (void)u; sk_logger_error("load failed: %s", p); }

static void on_init(void *user_data)
{
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;
    g_bg = sk_color_rgba(18, 20, 28, 255);
    sk_asset_add_task(sk_asset_ensure_async(MUSIC_PATH, NULL, 0), on_music_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(CLICK_PATH, NULL, 0), on_click_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    sk_keyboard_state_t kb = sk_input_get_keyboard_state();

    if (kb.keys[SK_KEY_SPACE] == SK_BUTTON_PRESSED && g_click != 0) {
        sk_sound_play(g_click);
    }
    if (kb.keys[SK_KEY_S] == SK_BUTTON_PRESSED) {
        const double until = sk_get_time() + 0.3; /* a deliberately slow frame */
        while (sk_get_time() < until) {
        }
    }
    if (kb.keys[SK_KEY_M] == SK_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { sk_sound_pause(g_music); } else { sk_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_text_draw("libsk + sokol_audio", 24, 30, 28, SK_COLOR_RAYWHITE);
    sk_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, streamed, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, SK_COLOR_SKYBLUE);
    sk_text_draw(g_click != 0 ? "click: ready (ogg)" : "click: loading...",
                 24, 110, 18, SK_COLOR_LIME);
    sk_text_draw("[SPACE] play click   [M] toggle music   [S] stall 300 ms   [ESC] quit",
                 24, 150, 16, SK_COLOR_LIGHTGRAY);

    sk_text_draw_fps(24, 12);
    sk_render_end();

    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(720, 240, "libsk audio", SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
