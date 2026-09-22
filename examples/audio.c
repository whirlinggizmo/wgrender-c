/* libwgrender audio example — looping mp3 music + a one-shot ogg sound.
 *
 * Each file is ensured local (async), then wgr_audio_create(path) makes a shared
 * Audio resource: the 6 MB music is streamed (decoded while playing), the small
 * click is decoded up front. Sound objects play them. On desktop mixing runs on the
 * audio device's thread, so music keeps playing through a slow frame: press S to
 * stall one frame for 300 ms and hear it not care. On the web it stutters instead:
 * sokol_audio's WebAudio callback (a ScriptProcessorNode) runs on the main thread,
 * the one the stall blocks, and its ~46 ms buffer runs dry. Press SPACE to play the
 * click, M to toggle music. */
#include <stddef.h>

#include "wgr.h"
#include "example_assets.h"

#define MUSIC_PATH "music/ethernight_club.mp3"
#define CLICK_PATH "sounds/click_004.ogg"

static wgr_color_t g_bg;
static wgr_handle_t g_music;
static wgr_handle_t g_click;
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

static void on_click_loaded(const char *path, void *user)
{
    wgr_handle_t audio = wgr_audio_create(path);
    (void)user;
    g_click = wgr_sound_create(audio);
    wgr_audio_release(audio); /* the sound object holds its own reference */
    wgr_sound_set_volume(g_click, 1.0f);
}

static void on_failed(const char *p, void *u) { (void)u; wgr_logger_error("load failed: %s", p); }

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    (void)user_data;
    g_bg = wgr_color_rgba(18, 20, 28, 255);
    wgr_asset_add_task(wgr_asset_ensure_async(MUSIC_PATH, NULL, 0), on_music_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(CLICK_PATH, NULL, 0), on_click_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();

    if (kb.keys[WGR_KEY_SPACE] == WGR_BUTTON_PRESSED && g_click != 0) {
        wgr_sound_play(g_click);
    }
    if (kb.keys[WGR_KEY_S] == WGR_BUTTON_PRESSED) {
        const double until = wgr_get_time() + 0.3; /* a deliberately slow frame */
        while (wgr_get_time() < until) {
        }
    }
    if (kb.keys[WGR_KEY_M] == WGR_BUTTON_PRESSED && g_music != 0) {
        if (g_music_on) { wgr_sound_pause(g_music); } else { wgr_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    wgr_render_begin();
    wgr_render_clear_background(g_bg);

    wgr_text_draw("libwgrender + sokol_audio", 24, 30, 28, WGR_COLOR_RAYWHITE);
    wgr_text_draw(g_music != 0 ? (g_music_on ? "music: playing (mp3, streamed, looping)"
                                            : "music: paused")
                              : "music: loading...",
                 24, 80, 18, WGR_COLOR_SKYBLUE);
    wgr_text_draw(g_click != 0 ? "click: ready (ogg)" : "click: loading...",
                 24, 110, 18, WGR_COLOR_LIME);
    wgr_text_draw("[SPACE] play click   [M] toggle music   [S] stall 300 ms   [ESC] quit",
                 24, 150, 16, WGR_COLOR_LIGHTGRAY);

    wgr_text_draw_fps(24, 12);
    wgr_render_end();

    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(720, 240, "libwgrender audio", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
