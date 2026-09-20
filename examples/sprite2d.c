/* libwgrender sprite2d example — screen-space sprites over a 3D scene.
 *
 *   - "sheet": the 256x256 logo treated as a 2x2 sprite sheet; set_source steps
 *     through the four quadrants like animation frames
 *   - "spin": rotates around its center (default pivot)
 *   - "swing": rotates around its top-left corner (pivot 0,0)
 *   - "flip": mirrored with a negative x scale
 *   - "tint": a white copy of the logo cycling through tint colors (tint multiplies
 *     the texture color, so it can't show on the black logo)
 *   - a one-off wgr_texture_draw in the corner (no object)
 * Sprites are scene members, so they draw on top of the 3D model and are picked
 * first. Hovering enlarges the sprite under the mouse; the alpha test lets the
 * pointer through the logo's transparent parts. Uses high-DPI, so all
 * positions are logical pixels. ESC quits. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define LOGO_PATH "sprites/logo/wg-logo-bw-alpha.png"
#define WHITE_LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define MODEL_PATH "models/gumshoe/gumshoe.glb"

enum { SPRITE_COUNT = 5, TINT_SPRITE = 4, PALETTE_SIZE = 24 };

static const char *NAMES[SPRITE_COUNT] = {"sheet", "spin", "swing", "flip", "tint"};

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_color_t bg;
    wgr_handle_t logo;
    wgr_color_t palette[PALETTE_SIZE];
    wgr_handle_t model;
    wgr_handle_t sprites[SPRITE_COUNT];
    wgr_handle_t hovered;
    float time;
    int frame;
} g;

static void on_logo_loaded(const char *path, void *user)
{
    (void)user;
    g.logo = wgr_texture_create(path);
    for (int i = 0; i < SPRITE_COUNT; i++) {
        if (i != TINT_SPRITE) {
            wgr_sprite2d_set_texture(g.sprites[i], g.logo);
        }
    }
}

static void on_white_logo_loaded(const char *path, void *user)
{
    wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_sprite2d_set_texture(g.sprites[TINT_SPRITE], texture);
    wgr_texture_release(texture); /* the sprite holds its own reference */
}

static void on_mesh_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.model, mesh);
    wgr_mesh_release(mesh);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = wgr_color_rgba(28, 30, 38, 255);
    for (int i = 0; i < PALETTE_SIZE; i++) { /* colors are immutable, so cycle a palette */
        const float a = (float)i / PALETTE_SIZE * 6.2831853f;
        g.palette[i] = wgr_color_rgba((int)(127 + 127 * sinf(a)), (int)(127 + 127 * sinf(a + 2.1f)),
                                       (int)(127 + 127 * sinf(a + 4.2f)), 255);
    }

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 1.4f, 5.5f, 0, 1, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.5f, -1.0f, -0.7f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(g.scene, sun, 0);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.35f);

    g.model = wgr_model_create(0);
    wgr_model_set_animation(g.model, 3);
    wgr_model_set_animation_loop(g.model, true);
    wgr_scene_add(g.scene, g.model, 0);

    for (int i = 0; i < SPRITE_COUNT; i++) {
        g.sprites[i] = wgr_sprite2d_create(0); /* texture attached when it loads */
        wgr_sprite2d_set_size(g.sprites[i], 128, 128);
        wgr_sprite2d_set_pick_alpha_test(g.sprites[i], true, 0.5f);
        wgr_scene_add(g.scene, g.sprites[i], 1);
    }
    wgr_sprite2d_set_position(g.sprites[0], 140, 170);
    wgr_sprite2d_set_position(g.sprites[1], 140, 380);
    wgr_sprite2d_set_pivot(g.sprites[2], 0, 0);
    wgr_sprite2d_set_position(g.sprites[2], 700, 110);
    wgr_sprite2d_set_size(g.sprites[2], 96, 96);
    wgr_sprite2d_set_position(g.sprites[3], 760, 400);
    wgr_sprite2d_set_scale(g.sprites[3], -1, 1);
    wgr_sprite2d_set_position(g.sprites[TINT_SPRITE], 450, 110);
    wgr_sprite2d_set_size(g.sprites[TINT_SPRITE], 96, 96);

    wgr_asset_add_task(wgr_asset_ensure_async(LOGO_PATH, NULL, WGR_ASSET_NONE), on_logo_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(WHITE_LOGO_PATH, NULL, WGR_ASSET_NONE), on_white_logo_loaded, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(MODEL_PATH, NULL, WGR_ASSET_NONE), on_mesh_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)tick_fraction;
    (void)user_data;
    wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    wgr_pick_result_t pick;
    const char *hover_name = "nothing";
    char line[128];

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
    g.time += dt;
    wgr_model_animate(g.model, dt);

    /* sprite sheet: one quadrant of the 256x256 texture per animation frame */
    g.frame = (int)(g.time * 2.0f) % 4;
    wgr_sprite2d_set_source(g.sprites[0], (float)(g.frame % 2) * 128, (float)(g.frame / 2) * 128, 128, 128);
    wgr_sprite2d_set_rotation(g.sprites[1], g.time);
    wgr_sprite2d_set_rotation(g.sprites[2], sinf(g.time * 1.5f) * 0.8f);
    wgr_sprite2d_set_tint(g.sprites[TINT_SPRITE], g.palette[(int)(g.time * 6.0f) % PALETTE_SIZE]);

    /* hover: 2D sprites are picked before the model behind them */
    pick = wgr_scene_pick(g.scene, 0, (float)mouse.x, (float)mouse.y);
    g.hovered = pick.hit ? pick.handle : 0;
    for (int i = 0; i < SPRITE_COUNT; i++) {
        const float grow = g.sprites[i] == g.hovered ? 1.15f : 1.0f;
        wgr_sprite2d_set_scale(g.sprites[i], i == 3 ? -grow : grow, grow); /* "flip" keeps its mirror */
        if (g.sprites[i] == g.hovered) {
            hover_name = NAMES[i];
        }
    }
    if (pick.hit && pick.handle == g.model) {
        hover_name = "model";
    }

    wgr_render_begin();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    wgr_texture_draw(g.logo, wgr_window_get_screen_size().x - 74, 10, 64, 64, WGR_COLOR_WHITE); /* one-off, no object */

    wgr_text_draw("libwgrender sprite2d: source rect, pivot, rotation, flip, picking", 12, 12, 16, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "mouse (%d, %d)  hover: %s  sheet frame %d", mouse.x, mouse.y, hover_name, g.frame);
    wgr_text_draw(line, 12, 36, 16, WGR_COLOR_LIGHTGRAY);
    wgr_render_end();
}

int main(void)
{
    wgr_init_values(900, 520, "libwgrender sprite2d", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
