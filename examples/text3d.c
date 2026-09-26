/* libwgrender text3d example — text in the 3D world, 3D shapes and object picking.
 *
 *   - camera-facing labels above a cube, a sphere and a rectangle
 *   - a sign: text with FREE facing, turned with its transform
 *   - circle outlines on the ground and a line-strip spiral built point by point
 *   - hover: wgr_scene_pick finds the object under the mouse (labels included) and
 *     highlights it; pick statistics and the FPS are drawn with a TrueType font
 *   - P toggles whether the cube is pickable
 * ESC quits. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define FONT_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"

enum { LABEL_COUNT = 3 };

static struct {
    wgr_handle_t scene;
    wgr_handle_t camera;
    wgr_handle_t font;
    wgr_color_t bg, grey, gold, teal, rose, highlight, ring;
    wgr_handle_t cube, sphere, panel, spiral, sign;
    wgr_handle_t rings[3];
    wgr_handle_t labels[LABEL_COUNT];
    wgr_handle_t hovered;
    float time;
} g;

static void on_font_loaded(const char *path, void *user)
{
    (void)user;
    g.font = wgr_font_create(path);
    for (int i = 0; i < LABEL_COUNT; i++) {
        wgr_text3d_set_font(g.labels[i], g.font);
    }
    wgr_text3d_set_font(g.sign, g.font);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static wgr_handle_t add_label(const char *text, float x, float y, float z)
{
    wgr_handle_t label = wgr_text3d_create(0); /* font attached when it loads */
    wgr_text3d_set_text(label, text);
    wgr_text3d_set_size(label, 0.35f);
    wgr_text3d_set_transform(label, x, y, z, 0, 0, 0);
    wgr_text3d_set_color(label, WGR_COLOR_RAYWHITE);
    wgr_scene_add(g.scene, label, 0);
    return label;
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    g.bg = wgr_color_rgba(22, 24, 30, 255);
    g.grey = wgr_color_rgba(60, 64, 76, 255);
    g.gold = wgr_color_rgba(230, 180, 60, 255);
    g.teal = wgr_color_rgba(60, 190, 180, 255);
    g.rose = wgr_color_rgba(220, 90, 120, 255);
    g.highlight = wgr_color_rgba(255, 255, 255, 255);
    g.ring = wgr_color_rgba(120, 130, 160, 255);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 4.0f, 9.0f, 0, 0.8f, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);

    g.cube = wgr_shape3d_create();
    wgr_shape3d_set_cube(g.cube, 1.2f, 1.2f, 1.2f);
    wgr_shape3d_set_transform(g.cube, -3, 0.6f, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.cube, 0);

    g.sphere = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g.sphere, 0.7f);
    wgr_shape3d_set_transform(g.sphere, 0, 0.7f, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.sphere, 0);

    g.panel = wgr_shape3d_create();
    wgr_shape3d_set_rectangle(g.panel, 1.4f, 1.0f);
    wgr_shape3d_set_transform(g.panel, 3, 0.8f, 0, 0, -0.5f, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.panel, 0);

    for (int i = 0; i < 3; i++) { /* rings lying on the ground under each object */
        g.rings[i] = wgr_shape3d_create();
        wgr_shape3d_set_circle(g.rings[i], 1.0f);
        wgr_shape3d_set_transform(g.rings[i], -3.0f + 3.0f * i, 0.01f, 0, -1.5707963f, 0, 0, 1, 1, 1);
        wgr_shape3d_set_color(g.rings[i], g.ring);
        wgr_shape3d_set_pickable(g.rings[i], false);
        wgr_scene_add(g.scene, g.rings[i], 0);
    }

    g.spiral = wgr_shape3d_create(); /* a line strip, built point by point */
    wgr_shape3d_set_line_strip(g.spiral);
    for (int i = 0; i <= 160; i++) {
        const float t = (float)i / 160.0f, a = t * 6.2831853f * 4.0f;
        wgr_shape3d_add_point(g.spiral, cosf(a) * (0.2f + t), t * 2.5f, sinf(a) * (0.2f + t));
    }
    wgr_shape3d_set_transform(g.spiral, 0, 0, -3, 0, 0, 0, 1, 1, 1);
    wgr_shape3d_set_color(g.spiral, g.teal);
    wgr_scene_add(g.scene, g.spiral, 0);

    g.labels[0] = add_label("cube", -3, 1.7f, 0);
    g.labels[1] = add_label("sphere", 0, 1.9f, 0);
    g.labels[2] = add_label("rectangle", 3, 1.8f, 0);

    g.sign = wgr_text3d_create(0); /* FREE facing: oriented by its rotation, like a sign */
    wgr_text3d_set_text(g.sign, "libwgrender text3d");
    wgr_text3d_set_size(g.sign, 0.6f);
    wgr_text3d_set_facing(g.sign, WGR_SPRITE3D_FACING_FREE);
    wgr_text3d_set_color(g.sign, g.gold);
    wgr_scene_add(g.scene, g.sign, 0);

    wgr_asset_add_task(wgr_asset_ensure_async(FONT_PATH, NULL, WGR_ASSET_NONE), on_font_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    const wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (kb.keys[WGR_KEY_P] == WGR_BUTTON_PRESSED) wgr_shape3d_set_pickable(g.cube, !wgr_shape3d_is_pickable(g.cube));

    g.time += dt;
    wgr_shape3d_set_transform(g.cube, -3, 0.6f, 0, 0, g.time * 0.7f, 0, 1, 1, 1);
    wgr_text3d_set_transform(g.sign, 0, 3.2f, -3, 0, sinf(g.time * 0.6f) * 0.6f, 0);

    /* hover: the nearest pickable object under the mouse */
    wgr_pick_reset_stats();
    const wgr_pick_result_t pick = wgr_scene_pick(g.scene, 0, (float)mouse.x, (float)mouse.y);
    g.hovered = pick.hit ? pick.handle : 0;
    wgr_shape3d_set_color(g.cube, g.hovered == g.cube ? g.highlight : g.gold);
    wgr_shape3d_set_color(g.sphere, g.hovered == g.sphere ? g.highlight : g.rose);
    wgr_shape3d_set_color(g.panel, g.hovered == g.panel ? g.highlight : g.teal);
    for (int i = 0; i < LABEL_COUNT; i++) {
        wgr_text3d_set_color(g.labels[i], g.hovered == g.labels[i] ? g.gold : WGR_COLOR_RAYWHITE);
    }
    const wgr_pick_stats_t stats = wgr_pick_get_stats();

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_grid(16, 1.0f, g.grey);
    wgr_render_end_mode_3d();
    wgr_scene_draw(g.scene);

    wgr_text_draw_ex(g.font, "libwgrender text3d: text in 3D, shapes, picking", 12, 10, 20, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "hover: %s   [P] cube pickable: %s",
             pick.hit ? (pick.handle == g.cube ? "cube" : pick.handle == g.sphere ? "sphere"
                         : pick.handle == g.panel ? "rectangle" : pick.handle == g.sign ? "sign" : "label")
                      : "nothing",
             wgr_shape3d_is_pickable(g.cube) ? "yes" : "no");
    wgr_text_draw_ex(g.font, line, 12, 36, 16, WGR_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "pick stats: %d box tests (%d rejected), %d exact tests, %d hits",
             stats.broadphase_tests, stats.broadphase_rejects, stats.narrowphase_tests, stats.narrowphase_hits);
    wgr_text_draw_ex(g.font, line, 12, 56, 16, WGR_COLOR_LIGHTGRAY);
    wgr_text_draw_fps_ex(g.font, 12, 80, 16, WGR_COLOR_LIME);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 640, "libwgrender text3d", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
