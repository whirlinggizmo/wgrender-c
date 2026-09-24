/* libwgrender font example — TrueType text via fontstash, loaded async.
 *
 * Loads two fonts (JetBrains Mono, Komika) with wgr_asset_ensure_async, then draws
 * scalable text including a measured, centered line.
 *
 *   D    switch the default font (wgr_text_draw, font handle 0) between the built-in
 *        font (JetBrains Mono, ASCII) and Komika
 *   ESC  quit */
#include <stddef.h>
#include <stdio.h>

#include "wgr.h"
#include "example_assets.h"

#define JETBRAINS_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define KOMIKA_PATH    "fonts/Komika/KOMIKAH_.ttf"

static wgr_color_t g_bg;
static wgr_handle_t g_mono;
static wgr_handle_t g_komika;

static void on_mono_loaded(const char *path, void *user)
{
    (void)user;
    g_mono = wgr_font_create(path);
}
static void on_komika_loaded(const char *path, void *user)
{
    (void)user;
    g_komika = wgr_font_create(path);
}
static void on_failed(const char *p, void *u)
{
    (void)u;
    wgr_logger_error("font load failed: %s", p);
}

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;
    g_bg = wgr_color_rgba(248, 248, 250, 255);
    wgr_asset_add_task(wgr_asset_ensure_async(JETBRAINS_PATH, NULL, 0), on_mono_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(KOMIKA_PATH, NULL, 0), on_komika_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    vec2_t screen = wgr_window_get_screen_size();

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    /* centered title (Komika), measured */
    const char *title = "libwgrender + fontstash";
    if (g_komika != 0) {
        vec2_t sz = wgr_text_measure_ex(g_komika, title, 56.0f);
        wgr_text_draw_ex(g_komika, title, (screen.x - sz.x) * 0.5f, 90.0f, 56.0f,
                        WGR_COLOR_DARKBLUE);
    }

    /* a few sizes of mono text */
    if (g_mono != 0) {
        wgr_text_draw_ex(g_mono, "The quick brown fox jumps over the lazy dog.",
                        40.0f, 200.0f, 28.0f, WGR_COLOR_BLACK);
        wgr_text_draw_ex(g_mono, "scalable, anti-aliased TrueType glyphs",
                        40.0f, 250.0f, 20.0f, WGR_COLOR_DARKGRAY);
        wgr_text_draw_ex(g_mono, "0123456789  !@#$%^&*()  +-*/=",
                        40.0f, 290.0f, 24.0f, WGR_COLOR_MAROON);
    } else {
        wgr_text_draw("loading fonts...", 40, 200, 20, WGR_COLOR_GRAY);
    }

    /* the default font: built in (JetBrains Mono), or Komika after D */
    wgr_keyboard_state_t keys = wgr_input_get_keyboard_state();
    if (keys.keys[WGR_KEY_D] == WGR_BUTTON_PRESSED && g_komika != 0) {
        wgr_text_set_default_font(wgr_text_get_default_font() == 0 ? g_komika : 0);
    }
    wgr_text_draw(wgr_text_get_default_font() == 0 ? "[D] default font: built in   {a|b} ~ \\ ^_`"
                                                 : "[D] default font: Komika   {a|b} ~ \\ ^_`",
                 40, 360, 16, WGR_COLOR_DARKGREEN);

    /* FPS in the default font */
    wgr_text_draw_fps(12, 12);

    wgr_render_end_frame();

    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(900, 500, "libwgrender font", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
