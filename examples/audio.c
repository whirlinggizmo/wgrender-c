/* libsk audio example — looping mp3 music + a one-shot ogg sound.
 *
 * Music is loaded async (sokol_fetch) then decoded (dr_mp3); the click sound is
 * decoded with stb_vorbis. Press SPACE to play the click, M to toggle music. */
#include <stddef.h>

#include "sk.h"

#define MUSIC_PATH "examples/assets/music/ethernight_club.mp3"
#define CLICK_PATH "examples/assets/sounds/click_004.ogg"

static sk_handle_t g_bg;
static sk_handle_t g_music;
static sk_handle_t g_click;
static bool g_music_on;

static void on_music_loaded(const char *p, const unsigned char *d, int n, void *u)
{
    (void)p; (void)u;
    g_music = sk_music_create_from_memory(d, n, MUSIC_PATH);
    sk_music_set_volume(g_music, 0.5f);
    sk_music_set_loop(g_music, true);
    sk_music_play(g_music);
    g_music_on = true;
}

static void on_click_loaded(const char *p, const unsigned char *d, int n, void *u)
{
    (void)p; (void)u;
    g_click = sk_sound_create_from_memory(d, n, CLICK_PATH);
    sk_sound_set_volume(g_click, 1.0f);
}

static void on_failed(const char *p, void *u) { (void)u; sk_logger_error("load failed: %s", p); }

static void on_init(void *user_data)
{
    (void)user_data;
    g_bg = sk_color_create(18, 20, 28, 255);
    sk_asset_load_async(MUSIC_PATH, on_music_loaded, on_failed, NULL);
    sk_asset_load_async(CLICK_PATH, on_click_loaded, on_failed, NULL);
}

static void frame(void *user_data)
{
    (void)user_data;
    sk_keyboard_state_t kb = sk_input_get_keyboard_state();

    if (kb.keys[SK_KEY_SPACE] == SK_BUTTON_PRESSED && g_click != 0) {
        sk_sound_play(g_click);
    }
    if (kb.keys[SK_KEY_M] == SK_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { sk_music_pause(g_music); } else { sk_music_play(g_music); }
        g_music_on = !g_music_on;
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_text_draw("libsk + sokol_audio", 24, 30, 28, SK_COLOR_RAYWHITE);
    sk_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, SK_COLOR_SKYBLUE);
    sk_text_draw(g_click != 0 ? "click: ready (ogg)" : "click: loading...",
                 24, 110, 18, SK_COLOR_LIME);
    sk_text_draw("[SPACE] play click   [M] toggle music   [ESC] quit",
                 24, 150, 16, SK_COLOR_LIGHTGRAY);

    sk_text_draw_fps(24, 12);
    sk_render_end();

    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(720, 240, "libsk audio", 0);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
