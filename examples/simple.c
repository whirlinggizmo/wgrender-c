/* libwgrender simple example — a port of librl's examples/c-simple, the reference scene
 * shared by librl's C, Haxe and Nim "simple" examples. It checks functional
 * parity with librl (same scene and behavior, not the same API).
 *
 * Scene: an animated model, a bobbing 3D sprite, looping music, two TTF fonts,
 * a centered message that reports what the mouse is over (scene picking), and a
 * debug overlay with timers, mouse state and the platform name.
 *
 * Left out on purpose: the rt_boot/rt_init/rt_tick host loop (sokol owns the
 * loop) and the reload counter (scripting hot reload lives outside the core). */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define DEBUG_FONT_PATH  "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"
#define KOMIKA_FONT_PATH "fonts/Komika/KOMIKAH_.ttf"
#define MODEL_PATH       "models/gumshoe/gumshoe.glb"
#define SPRITE_PATH      "sprites/logo/wg-logo-bw-alpha.png"
#define BGM_PATH         "music/ethernight_club.mp3"

enum {
    SCREEN_WIDTH = 1024,
    SCREEN_HEIGHT = 1280,
    DEBUG_FONT_SIZE = 18,
    KOMIKA_FONT_SIZE = 24,
    TEXT_CAPACITY = 256,
};

static const float SPRITE_Y_OFFSET = 3.0f;
static const float BOB_SPEED = 1.0f;
static const float BOB_HEIGHT = 1.5f;

static struct {
    float elapsed;
    float countdown_timer;
    wgr_handle_t debug_font;
    wgr_color_t grey_alpha;
    wgr_handle_t komika_font;
    wgr_handle_t sprite;
    wgr_handle_t model;
    wgr_handle_t bgm;
    wgr_handle_t camera;
    wgr_handle_t scene;
    wgr_color_t background_color;
    char message[TEXT_CAPACITY];
    char platform_text[TEXT_CAPACITY];
} g;

/* --- asset callbacks: path is local and ready; create the resource, then the object --- */

static void on_bgm_ready(const char *path, void *user)
{
    wgr_handle_t audio = wgr_audio_create(path);
    (void)user;
    g.bgm = wgr_sound_create(audio);
    wgr_audio_release(audio); /* the sound holds its own reference */
    wgr_sound_set_loop(g.bgm, true);
    wgr_sound_play(g.bgm);
}

static void on_model_ready(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    g.model = wgr_model_create(mesh);
    wgr_mesh_release(mesh); /* the model holds its own reference */
    wgr_model_set_animation(g.model, 1);
    wgr_model_set_animation_speed(g.model, 1.0f);
    wgr_model_set_animation_loop(g.model, true);
    wgr_model_set_transform(g.model, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    wgr_model_set_tint(g.model, WGR_COLOR_RAYWHITE);
    wgr_scene_add(g.scene, g.model, 0);
}

static void on_sprite_ready(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    g.sprite = wgr_sprite3d_create(texture);
    wgr_texture_release(texture); /* the sprite holds its own reference */
    wgr_sprite3d_set_facing(g.sprite, WGR_SPRITE3D_FACING_FREE); /* librl's default: oriented by its rotation */
    wgr_sprite3d_set_transform(g.sprite, 0, SPRITE_Y_OFFSET, 0, 0, 0, 0, 1, 1, 1);
    wgr_sprite3d_set_tint(g.sprite, WGR_COLOR_RAYWHITE);
    wgr_scene_add(g.scene, g.sprite, 0);
}

/* Fonts are sized per draw call in libwgrender, so one font handle serves any size. */
static void on_debug_font_ready(const char *path, void *user)
{
    (void)user;
    g.debug_font = wgr_font_create(path);
}

static void on_komika_font_ready(const char *path, void *user)
{
    (void)user;
    g.komika_font = wgr_font_create(path);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("failed to import asset: %s", path);
}

static void load(const char *path, wgr_asset_callback_fn on_ready)
{
    wgr_handle_t task = wgr_asset_ensure_async(path, NULL, WGR_ASSET_NONE);
    if (wgr_asset_add_task(task, on_ready, on_failed, NULL) != WGR_ASSET_ADD_TASK_OK) {
        on_failed(path, NULL);
    }
}

/* --- lifecycle --- */

static void on_init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);
    wgr_set_target_fps(60);

    g.countdown_timer = 30.0f;
    snprintf(g.message, sizeof(g.message), "Hello from libwgrender simple!");
    snprintf(g.platform_text, sizeof(g.platform_text), "Platform: %s", wgr_get_platform());

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE); /* default fov: pi/4 (45 degrees) */
    wgr_camera3d_set_view(g.camera, 12, 12, 12, 0, 1, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);

    /* same lighting as librl's c-simple: a directional light plus ambient 0.25 */
    wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(g.scene, sun, 0);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.25f);
    g.background_color = wgr_color_rgba(245, 245, 245, 255);
    g.grey_alpha = wgr_color_rgba(0, 0, 0, 128);

    load(BGM_PATH, on_bgm_ready);
    load(MODEL_PATH, on_model_ready);
    load(SPRITE_PATH, on_sprite_ready);
    load(DEBUG_FONT_PATH, on_debug_font_ready);
    load(KOMIKA_FONT_PATH, on_komika_font_ready);
}

static void update(float dt)
{
    g.elapsed += dt;
    g.countdown_timer -= dt;

    if (g.model != 0) {
        wgr_model_animate(g.model, dt);
    }
    if (g.sprite != 0) {
        float y = sinf(g.elapsed * BOB_SPEED) * BOB_HEIGHT + SPRITE_Y_OFFSET;
        wgr_sprite3d_set_transform(g.sprite, 0, y, 0, 0, 0, 0, 1, 1, 1);
    }
}

static void update_pick_message(wgr_mouse_state_t mouse)
{
    wgr_pick_result_t pick = wgr_scene_pick(g.scene, 0, (float)mouse.x, (float)mouse.y);
    const char *what = !pick.hit                ? NULL
                       : pick.handle == g.model  ? "Model"
                       : pick.handle == g.sprite ? "Sprite"
                                                 : NULL;
    if (what == NULL) {
        snprintf(g.message, sizeof(g.message), "Nothing picked!");
        return;
    }
    snprintf(g.message, sizeof(g.message),
             "%s pick: Mouse position (mouse.x:%d, mouse.y:%d) pick result y: %f",
             what, mouse.x, mouse.y, pick.point_world.y);
}

/* Draw with the TTF font once it's loaded, the built-in font until then. */
static void draw_text(wgr_handle_t font, const char *text, float x, float y, int size,
                      wgr_handle_t color)
{
    if (font != 0) {
        wgr_text_draw_ex(font, text, x, y, (float)size, color);
    } else {
        wgr_text_draw(text, (int)x, (int)y, size, color);
    }
}

static void draw_centered_message(void)
{
    vec2_t screen = wgr_window_get_screen_size();
    vec2_t size = g.komika_font != 0
                      ? wgr_text_measure_ex(g.komika_font, g.message, (float)KOMIKA_FONT_SIZE)
                      : (vec2_t){(float)wgr_text_measure(g.message, KOMIKA_FONT_SIZE),
                                 (float)KOMIKA_FONT_SIZE};
    draw_text(g.komika_font, g.message, (screen.x - size.x) / 2.0f,
              (screen.y - size.y) / 2.0f, KOMIKA_FONT_SIZE, WGR_COLOR_BLUE);
}

static void draw_overlay(wgr_mouse_state_t mouse)
{
    char line[TEXT_CAPACITY];

    snprintf(line, sizeof(line), "Remaining: %.2f", g.countdown_timer);
    draw_text(g.debug_font, line, 10, 36, DEBUG_FONT_SIZE, WGR_COLOR_BLACK);
    snprintf(line, sizeof(line), "Elapsed: %.2f", g.elapsed);
    draw_text(g.debug_font, line, 10, 56, DEBUG_FONT_SIZE, WGR_COLOR_BLACK);
    snprintf(line, sizeof(line), "Mouse: (%d, %d) w:%.1f b:[%d, %d, %d]", mouse.x, mouse.y,
             (double)mouse.wheel, mouse.left, mouse.right, mouse.middle);
    draw_text(g.debug_font, line, 10, 76, DEBUG_FONT_SIZE, WGR_COLOR_BLACK);
    draw_text(g.debug_font, g.platform_text, 10, 96, DEBUG_FONT_SIZE, WGR_COLOR_BLACK);

    wgr_text_draw_fps_ex(g.debug_font, 10, 10, DEBUG_FONT_SIZE, g.grey_alpha);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();

    update(dt);
    update_pick_message(mouse);

    wgr_render_begin();
    wgr_render_clear_background(g.background_color);
    wgr_scene_draw(g.scene);
    draw_centered_message();
    draw_overlay(mouse);
    wgr_render_end();
}

int main(void)
{
    wgr_init_values(SCREEN_WIDTH, SCREEN_HEIGHT, "simple (libwgrender)", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
