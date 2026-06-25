/* libsk font example — TrueType text via fontstash, loaded async.
 *
 * Loads two fonts (JetBrains Mono, Komika) with sk_asset_load_async, then draws
 * scalable text including a measured, centered line. */
#include <stddef.h>
#include <stdio.h>

#include "sk.h"

#define JETBRAINS_PATH "examples/assets/fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define KOMIKA_PATH    "examples/assets/fonts/Komika/KOMIKAH_.ttf"

static sk_handle_t g_bg;
static sk_handle_t g_mono;
static sk_handle_t g_komika;

static void on_mono_loaded(const char *path, void *user)
{
    (void)user;
    g_mono = sk_font_create(path);
}
static void on_komika_loaded(const char *path, void *user)
{
    (void)user;
    g_komika = sk_font_create(path);
}
static void on_failed(const char *p, void *u)
{
    (void)u;
    sk_logger_error("font load failed: %s", p);
}

static void on_init(void *user_data)
{
    (void)user_data;
    g_bg = sk_color_create(248, 248, 250, 255);
    sk_asset_add_task(sk_asset_ensure_async(JETBRAINS_PATH, NULL), on_mono_loaded, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(KOMIKA_PATH, NULL), on_komika_loaded, on_failed, NULL);
}

static void frame(void *user_data)
{
    (void)user_data;
    vec2_t screen = sk_window_get_screen_size();

    sk_render_begin();
    sk_render_clear_background(g_bg);

    /* centered title (Komika), measured */
    const char *title = "libsk + fontstash";
    if (g_komika != 0) {
        vec2_t sz = sk_text_measure_ex(g_komika, title, 56.0f);
        sk_text_draw_ex(g_komika, title, (screen.x - sz.x) * 0.5f, 90.0f, 56.0f,
                        SK_COLOR_DARKBLUE);
    }

    /* a few sizes of mono text */
    if (g_mono != 0) {
        sk_text_draw_ex(g_mono, "The quick brown fox jumps over the lazy dog.",
                        40.0f, 200.0f, 28.0f, SK_COLOR_BLACK);
        sk_text_draw_ex(g_mono, "scalable, anti-aliased TrueType glyphs",
                        40.0f, 250.0f, 20.0f, SK_COLOR_DARKGRAY);
        sk_text_draw_ex(g_mono, "0123456789  !@#$%^&*()  +-*/=",
                        40.0f, 290.0f, 24.0f, SK_COLOR_MAROON);
    } else {
        sk_text_draw("loading fonts...", 40, 200, 20, SK_COLOR_GRAY);
    }

    /* bitmap-font FPS overlay still available */
    sk_text_draw_fps(12, 12);

    sk_render_end();

    sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 500, "libsk font", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
