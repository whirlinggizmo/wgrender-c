/* libsk quit example — sk_request_quit while audio plays and files are loading.
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
#include "sk.h"

static struct {
    sk_handle_t music;
    sk_color_t bg;
    double quit_at;
    bool quitting;
} g;

static void on_music(const char *path, void *user)
{
    const sk_handle_t audio = sk_audio_create(path);
    (void)user;
    g.music = sk_sound_create(audio);
    sk_audio_release(audio); /* the sound keeps its own reference */
    sk_sound_set_loop(g.music, true);
    sk_sound_play(g.music);
}

static void on_environment(const char *path, void *user)
{
    (void)path;
    (void)user; /* only started to be in flight at quit; never created */
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_rgba(30, 36, 48, 255);
    g.quit_at = sk_get_time() + 1.0; /* soon enough for tools/webcheck.mjs to see the quit */
    sk_asset_add_task(sk_asset_ensure_async("music/ethernight_club.mp3", NULL, SK_ASSET_NONE), on_music, NULL,
                      NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    char line[96];

    (void)dt;
    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_Q] == SK_BUTTON_PRESSED || kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_text_draw("libsk quit   Q: quit now", 12, 12, 16, SK_COLOR_RAYWHITE);
    if (g.quitting) {
        sk_text_draw("quit requested: cleanup runs after this frame", 12, 40, 16, SK_COLOR_LIGHTGRAY);
    } else {
        snprintf(line, sizeof(line), "loading, stalling and quitting in %.1f s", g.quit_at - sk_get_time());
        sk_text_draw(line, 12, 40, 16, SK_COLOR_LIGHTGRAY);
    }
    sk_render_end();

    if (!g.quitting && sk_get_time() >= g.quit_at) {
        g.quitting = true;
        sk_asset_add_task(sk_asset_ensure_async("environments/venice_sunset_1k.hdr", NULL, SK_ASSET_NONE),
                          on_environment, NULL, NULL);
        sk_asset_add_task(sk_asset_ensure_async("environments/studio_small_09_1k.hdr", NULL, SK_ASSET_NONE),
                          on_environment, NULL, NULL);
        const double busy_until = sk_get_time() + 0.5; /* a long synchronous frame */
        while (sk_get_time() < busy_until) {
        }
        sk_request_quit();
    }
}

static void cleanup(void *user_data)
{
    (void)user_data;
    sk_logger_info("quit: cleanup");
}

int main(void)
{
    sk_init_values(640, 200, "libsk quit", 0);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    sk_set_cleanup(cleanup, NULL);
    return sk_run();
}
