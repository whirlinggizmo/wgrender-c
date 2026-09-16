/* libsk simple example — a port of librl's examples/c-simple, the reference scene
 * shared by librl's C, Haxe and Nim "simple" examples. It checks functional
 * parity with librl (same scene and behavior, not the same API).
 *
 * Scene: an animated model, a bobbing 3D sprite, looping music, two TTF fonts,
 * a centered message that reports what the mouse is over (scene picking), and a
 * debug overlay with timers, mouse state and the platform name.
 *
 * Differences from librl are marked PARITY below. Left out on purpose: the
 * rt_boot/rt_init/rt_tick host loop (sokol owns the loop) and the reload
 * counter (scripting hot reload lives outside the core). */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

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
    sk_handle_t debug_font;
    sk_handle_t komika_font;
    sk_handle_t sprite;
    sk_handle_t model;
    sk_handle_t bgm;
    sk_handle_t camera;
    sk_handle_t scene;
    sk_handle_t background_color;
    char message[TEXT_CAPACITY];
    char platform_text[TEXT_CAPACITY];
} g;

/* --- asset callbacks: path is local and ready; create the resource, then the object --- */

static void on_bgm_ready(const char *path, void *user)
{
    sk_handle_t audio = sk_audio_create(path);
    (void)user;
    g.bgm = sk_sound_create(audio);
    sk_audio_destroy(audio); /* the sound holds its own reference */
    sk_sound_set_loop(g.bgm, true);
    sk_sound_play(g.bgm);
}

static void on_model_ready(const char *path, void *user)
{
    sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    g.model = sk_model_create(mesh);
    sk_mesh_destroy(mesh); /* the model holds its own reference */
    sk_model_set_animation(g.model, 1);
    sk_model_set_animation_speed(g.model, 1.0f);
    sk_model_set_animation_loop(g.model, true);
    sk_model_set_transform(g.model, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    sk_model_set_tint(g.model, SK_COLOR_RAYWHITE);
    sk_scene_add(g.scene, g.model, 0);
}

static void on_sprite_ready(const char *path, void *user)
{
    sk_handle_t texture = sk_texture_create(path);
    (void)user;
    g.sprite = sk_sprite3d_create(texture);
    sk_texture_destroy(texture); /* the sprite holds its own reference */
    /* PARITY: librl's default facing is FREE (uses the transform's rotation);
     * libsk has no FREE facing, so the sprite uses the default (faces camera). */
    sk_sprite3d_set_transform(g.sprite, 0, SPRITE_Y_OFFSET, 0, 0, 0, 0, 1, 1, 1);
    sk_sprite3d_set_tint(g.sprite, SK_COLOR_RAYWHITE);
    sk_scene_add(g.scene, g.sprite, 0);
}

/* Fonts are sized per draw call in libsk, so one font handle serves any size. */
static void on_debug_font_ready(const char *path, void *user)
{
    (void)user;
    g.debug_font = sk_font_create(path);
}

static void on_komika_font_ready(const char *path, void *user)
{
    (void)user;
    g.komika_font = sk_font_create(path);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("failed to import asset: %s", path);
}

static void load(const char *path, sk_asset_callback_fn on_ready)
{
    sk_handle_t task = sk_asset_ensure_async(path, NULL, SK_ASSET_NONE);
    if (sk_asset_add_task(task, on_ready, on_failed, NULL) != SK_ASSET_ADD_TASK_OK) {
        on_failed(path, NULL);
    }
}

/* --- lifecycle --- */

static void on_init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    sk_logger_set_level(SK_LOGGER_LEVEL_WARN);
    sk_set_target_fps(60);

    g.countdown_timer = 30.0f;
    snprintf(g.message, sizeof(g.message), "Hello from libsk simple!");
    snprintf(g.platform_text, sizeof(g.platform_text), "Platform: %s", sk_get_platform());

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE); /* default fov: pi/4 (45 degrees) */
    sk_camera3d_set_view(g.camera, 12, 12, 12, 0, 1, 0, 0, 1, 0);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);

    /* same lighting as librl's c-simple: a directional light plus ambient 0.25 */
    sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
    sk_light_set_intensity(sun, 3.0f);
    sk_scene_add(g.scene, sun, 0);
    sk_scene_set_ambient(g.scene, 0, 0.25f);
    g.background_color = sk_color_create(245, 245, 245, 255);

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
        sk_model_animate(g.model, dt);
    }
    if (g.sprite != 0) {
        float y = sinf(g.elapsed * BOB_SPEED) * BOB_HEIGHT + SPRITE_Y_OFFSET;
        sk_sprite3d_set_transform(g.sprite, 0, y, 0, 0, 0, 0, 1, 1, 1);
    }
}

static void update_pick_message(sk_mouse_state_t mouse)
{
    sk_pick_result_t pick = sk_scene_pick(g.scene, 0, (float)mouse.x, (float)mouse.y);
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
static void draw_text(sk_handle_t font, const char *text, float x, float y, int size,
                      sk_handle_t color)
{
    if (font != 0) {
        sk_text_draw_ex(font, text, x, y, (float)size, color);
    } else {
        sk_text_draw(text, (int)x, (int)y, size, color);
    }
}

static void draw_centered_message(void)
{
    vec2_t screen = sk_window_get_screen_size();
    vec2_t size = g.komika_font != 0
                      ? sk_text_measure_ex(g.komika_font, g.message, (float)KOMIKA_FONT_SIZE)
                      : (vec2_t){(float)sk_text_measure(g.message, KOMIKA_FONT_SIZE),
                                 (float)KOMIKA_FONT_SIZE};
    draw_text(g.komika_font, g.message, (screen.x - size.x) / 2.0f,
              (screen.y - size.y) / 2.0f, KOMIKA_FONT_SIZE, SK_COLOR_BLUE);
}

static void draw_overlay(sk_mouse_state_t mouse)
{
    char line[TEXT_CAPACITY];

    snprintf(line, sizeof(line), "Remaining: %.2f", g.countdown_timer);
    draw_text(g.debug_font, line, 10, 36, DEBUG_FONT_SIZE, SK_COLOR_BLACK);
    snprintf(line, sizeof(line), "Elapsed: %.2f", g.elapsed);
    draw_text(g.debug_font, line, 10, 56, DEBUG_FONT_SIZE, SK_COLOR_BLACK);
    snprintf(line, sizeof(line), "Mouse: (%d, %d) w:%d b:[%d, %d, %d]", mouse.x, mouse.y,
             mouse.wheel, mouse.left, mouse.right, mouse.middle);
    draw_text(g.debug_font, line, 10, 76, DEBUG_FONT_SIZE, SK_COLOR_BLACK);
    draw_text(g.debug_font, g.platform_text, 10, 96, DEBUG_FONT_SIZE, SK_COLOR_BLACK);

    /* PARITY: librl draws the FPS in the debug font, translucent grey. libsk can
     * only draw it in the built-in font for now. */
    sk_text_draw_fps(10, 10);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;
    sk_mouse_state_t mouse = sk_input_get_mouse_state();

    update(dt);
    update_pick_message(mouse);

    sk_render_begin();
    sk_render_clear_background(g.background_color);
    sk_scene_draw(g.scene);
    draw_centered_message();
    draw_overlay(mouse);
    sk_render_end();
}

int main(void)
{
    sk_init_values(SCREEN_WIDTH, SCREEN_HEIGHT, "simple (libsk)", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
