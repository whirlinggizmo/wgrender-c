/* libwgrender quit example — wgr_request_quit while audio plays and files are loading.
 *
 * Plays music, and after a second starts loading two environments, keeps the main
 * thread busy for half a second (like a long synchronous load) and quits. That is
 * the path that used to crash on web: audio events queued during the busy frame
 * ran after shutdown. Cleanup then runs with loads still in progress.
 *
 * On web, quitting stops the frame loop and runs cleanup; the canvas keeps showing
 * the last frame.
 *
 *   Q    quit now
 *   ESC  quit now */
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

static struct {
    wgr_handle_t music;
    wgr_color_t bg;
    double quit_at;
    bool quitting;
} g;

static void on_music(const char *path, void *user)
{
    const wgr_handle_t audio = wgr_audio_create(path);
    (void)user;
    g.music = wgr_sound_create(audio);
    wgr_audio_release(audio); /* the sound keeps its own reference */
    wgr_sound_set_loop(g.music, true);
    wgr_sound_play(g.music);
}

static void on_environment(const char *path, void *user)
{
    (void)path;
    (void)user; /* only started to be in flight at quit; never created */
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(30, 36, 48, 255);
    g.quit_at = wgr_get_time() + 1.0; /* soon enough for tools/webcheck.mjs to see the quit */
    wgr_asset_add_task(wgr_asset_ensure_async("music/ethernight_club.mp3", NULL, WGR_ASSET_NONE), on_music, NULL,
                      NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    char line[96];

    (void)dt;
    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_Q] == WGR_BUTTON_PRESSED || kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }

    wgr_render_begin();
    wgr_render_clear_background(g.bg);
    wgr_text_draw("libwgrender quit   Q: quit now", 12, 12, 16, WGR_COLOR_RAYWHITE);
    if (g.quitting) {
        wgr_text_draw("quit requested: cleanup runs after this frame", 12, 40, 16, WGR_COLOR_LIGHTGRAY);
    } else {
        snprintf(line, sizeof(line), "loading, stalling and quitting in %.1f s", g.quit_at - wgr_get_time());
        wgr_text_draw(line, 12, 40, 16, WGR_COLOR_LIGHTGRAY);
    }
    wgr_render_end();

    if (!g.quitting && wgr_get_time() >= g.quit_at) {
        g.quitting = true;
        wgr_asset_add_task(wgr_asset_ensure_async("environments/venice_sunset_1k.hdr", NULL, WGR_ASSET_NONE),
                          on_environment, NULL, NULL);
        wgr_asset_add_task(wgr_asset_ensure_async("environments/studio_small_09_1k.hdr", NULL, WGR_ASSET_NONE),
                          on_environment, NULL, NULL);
        const double busy_until = wgr_get_time() + 0.5; /* a long synchronous frame */
        while (wgr_get_time() < busy_until) {
        }
        wgr_request_quit();
    }
}

static void cleanup(void *user_data)
{
    (void)user_data;
    wgr_logger_info("quit: cleanup");
}

int main(void)
{
    wgr_init_values(640, 200, "libwgrender quit", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    wgr_set_cleanup(cleanup, NULL);
    return wgr_run();
}
