/* libwgrender audio example — looping mp3 music + a one-shot ogg sound.
 *
 * wgr_audio_create(path) makes a shared Audio resource, loading on create: the music
 * (over 1 MB) is streamed (decoded while playing), the small click is decoded as it
 * loads. Sound objects play them, made at once: one playing an Audio still loading
 * waits, and starts when it's loaded. On desktop mixing runs on the
 * audio device's thread, so music keeps playing through a slow frame: press S to
 * stall one frame for 300 ms and hear it not care. On the web it stutters instead:
 * sokol_audio's WebAudio callback (a ScriptProcessorNode) runs on the main thread,
 * the one the stall blocks, and its ~46 ms buffer runs dry. Press SPACE to play the
 * click, M to toggle music. */
#include <stddef.h>

#include "wgr.h"
#include "shared/example_assets.h"

#define CLICK_PATH "sounds/click_004.ogg"

static wgr_color_t g_bg;
static wgr_handle_t g_music;
static wgr_handle_t g_click;
static bool g_music_on;

/* A sound playing the Audio at `path`; the sound holds the Audio's reference. */
static wgr_handle_t make_sound(const char *path)
{
    const wgr_handle_t audio = wgr_audio_create(path);
    const wgr_handle_t sound = wgr_sound_create(audio);
    wgr_resource_release(audio);
    return sound;
}

/* Whether a sound's Audio has loaded. */
static bool is_loaded(wgr_handle_t sound)
{
    return wgr_resource_get_status(wgr_sound_get_audio(sound)) == WGR_RESOURCE_READY;
}

static void on_init(void *user_data)
{
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    (void)user_data;
    g_bg = wgr_color_rgba(18, 20, 28, 255);
    g_music = make_sound(MUSIC_PATH);
    wgr_sound_set_volume(g_music, 0.5f);
    wgr_sound_set_loop(g_music, true); /* "music" is just a looping sound */
    wgr_sound_play(g_music);            /* plays once it has loaded */
    g_music_on = true;
    g_click = make_sound(CLICK_PATH);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();

    if (kb.keys[WGR_KEY_SPACE] == WGR_BUTTON_PRESSED) {
        wgr_sound_play(g_click);
    }
    if (kb.keys[WGR_KEY_S] == WGR_BUTTON_PRESSED) {
        const double until = wgr_get_time() + 0.3; /* a deliberately slow frame */
        while (wgr_get_time() < until) {
        }
    }
    if (kb.keys[WGR_KEY_M] == WGR_BUTTON_PRESSED) {
        if (g_music_on) { wgr_sound_pause(g_music); } else { wgr_sound_resume(g_music); }
        g_music_on = !g_music_on;
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(g_bg);

    wgr_text_draw("libwgrender + sokol_audio", 24, 30, 28, WGR_COLOR_RAYWHITE);
    wgr_text_draw(is_loaded(g_music) ? (g_music_on ? "music: playing (mp3, streamed, looping)"
                                                   : "music: paused")
                                     : "music: loading...",
                 24, 80, 18, WGR_COLOR_SKYBLUE);
    wgr_text_draw(is_loaded(g_click) ? "click: ready (ogg)" : "click: loading...",
                 24, 110, 18, WGR_COLOR_LIME);
#ifndef __EMSCRIPTEN__ /* the quit key's hint, as the key: desktop only */
    wgr_text_draw("[SPACE] play click   [M] toggle music   [S] stall 300 ms   [ESC] quit",
                 24, 150, 16, WGR_COLOR_LIGHTGRAY);
#else
    wgr_text_draw("[SPACE] play click   [M] toggle music   [S] stall 300 ms",
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
    wgr_init_values(720, 240, "libwgrender audio", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
