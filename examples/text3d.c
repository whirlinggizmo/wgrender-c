/* libsk text3d example — text in the 3D world, 3D shapes and object picking.
 *
 *   - camera-facing labels above a cube, a sphere and a rectangle
 *   - a sign: text with FREE facing, turned with its transform
 *   - circle outlines on the ground and a line-strip spiral built point by point
 *   - hover: sk_scene_pick finds the object under the mouse (labels included) and
 *     highlights it; pick statistics and the FPS are drawn with a TrueType font
 *   - P toggles whether the cube is pickable
 * ESC quits. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define FONT_PATH "fonts/JetBrainsMono/JetBrainsMono-Regular.ttf"

enum { LABEL_COUNT = 3 };

static struct {
    sk_handle_t scene;
    sk_handle_t camera;
    sk_handle_t font;
    sk_handle_t bg, grey, gold, teal, rose, highlight, ring;
    sk_handle_t cube, sphere, panel, spiral, sign;
    sk_handle_t rings[3];
    sk_handle_t labels[LABEL_COUNT];
    sk_handle_t hovered;
    float time;
} g;

static void on_font_loaded(const char *path, void *user)
{
    (void)user;
    g.font = sk_font_create(path);
    for (int i = 0; i < LABEL_COUNT; i++) {
        sk_text3d_set_font(g.labels[i], g.font);
    }
    sk_text3d_set_font(g.sign, g.font);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static sk_handle_t add_label(const char *text, float x, float y, float z)
{
    sk_handle_t label = sk_text3d_create(0); /* font attached when it loads */
    sk_text3d_set_text(label, text);
    sk_text3d_set_size(label, 0.35f);
    sk_text3d_set_transform(label, x, y, z, 0, 0, 0);
    sk_text3d_set_color(label, SK_COLOR_RAYWHITE);
    sk_scene_add(g.scene, label, 0);
    return label;
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_create(22, 24, 30, 255);
    g.grey = sk_color_create(60, 64, 76, 255);
    g.gold = sk_color_create(230, 180, 60, 255);
    g.teal = sk_color_create(60, 190, 180, 255);
    g.rose = sk_color_create(220, 90, 120, 255);
    g.highlight = sk_color_create(255, 255, 255, 255);
    g.ring = sk_color_create(120, 130, 160, 255);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g.camera, 0, 4.0f, 9.0f, 0, 0.8f, 0, 0, 1, 0);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);

    g.cube = sk_shape_create();
    sk_shape_set_cube(g.cube, 1.2f, 1.2f, 1.2f);
    sk_shape_set_transform(g.cube, -3, 0.6f, 0, 0, 0, 0, 1, 1, 1);
    sk_scene_add(g.scene, g.cube, 0);

    g.sphere = sk_shape_create();
    sk_shape_set_sphere(g.sphere, 0.7f);
    sk_shape_set_transform(g.sphere, 0, 0.7f, 0, 0, 0, 0, 1, 1, 1);
    sk_scene_add(g.scene, g.sphere, 0);

    g.panel = sk_shape_create();
    sk_shape_set_rectangle(g.panel, 1.4f, 1.0f);
    sk_shape_set_transform(g.panel, 3, 0.8f, 0, 0, -0.5f, 0, 1, 1, 1);
    sk_scene_add(g.scene, g.panel, 0);

    for (int i = 0; i < 3; i++) { /* rings lying on the ground under each object */
        g.rings[i] = sk_shape_create();
        sk_shape_set_circle(g.rings[i], 1.0f);
        sk_shape_set_transform(g.rings[i], -3.0f + 3.0f * i, 0.01f, 0, -1.5707963f, 0, 0, 1, 1, 1);
        sk_shape_set_color(g.rings[i], g.ring);
        sk_shape_set_pickable(g.rings[i], false);
        sk_scene_add(g.scene, g.rings[i], 0);
    }

    g.spiral = sk_shape_create(); /* a line strip, built point by point */
    sk_shape_set_line_strip(g.spiral);
    for (int i = 0; i <= 160; i++) {
        const float t = (float)i / 160.0f, a = t * 6.2831853f * 4.0f;
        sk_shape_add_point(g.spiral, cosf(a) * (0.2f + t), t * 2.5f, sinf(a) * (0.2f + t));
    }
    sk_shape_set_transform(g.spiral, 0, 0, -3, 0, 0, 0, 1, 1, 1);
    sk_shape_set_color(g.spiral, g.teal);
    sk_scene_add(g.scene, g.spiral, 0);

    g.labels[0] = add_label("cube", -3, 1.7f, 0);
    g.labels[1] = add_label("sphere", 0, 1.9f, 0);
    g.labels[2] = add_label("rectangle", 3, 1.8f, 0);

    g.sign = sk_text3d_create(0); /* FREE facing: oriented by its rotation, like a sign */
    sk_text3d_set_text(g.sign, "libsk text3d");
    sk_text3d_set_size(g.sign, 0.6f);
    sk_text3d_set_facing(g.sign, SK_SPRITE3D_FACING_FREE);
    sk_text3d_set_color(g.sign, g.gold);
    sk_scene_add(g.scene, g.sign, 0);

    sk_asset_add_task(sk_asset_ensure_async(FONT_PATH, NULL, SK_ASSET_NONE), on_font_loaded, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();
    if (kb.keys[SK_KEY_P] == SK_BUTTON_PRESSED) sk_shape_set_pickable(g.cube, !sk_shape_is_pickable(g.cube));

    g.time += dt;
    sk_shape_set_transform(g.cube, -3, 0.6f, 0, 0, g.time * 0.7f, 0, 1, 1, 1);
    sk_text3d_set_transform(g.sign, 0, 3.2f, -3, 0, sinf(g.time * 0.6f) * 0.6f, 0);

    /* hover: the nearest pickable object under the mouse */
    sk_pick_reset_stats();
    const sk_pick_result_t pick = sk_scene_pick(g.scene, 0, (float)mouse.x, (float)mouse.y);
    g.hovered = pick.hit ? pick.handle : 0;
    sk_shape_set_color(g.cube, g.hovered == g.cube ? g.highlight : g.gold);
    sk_shape_set_color(g.sphere, g.hovered == g.sphere ? g.highlight : g.rose);
    sk_shape_set_color(g.panel, g.hovered == g.panel ? g.highlight : g.teal);
    for (int i = 0; i < LABEL_COUNT; i++) {
        sk_text3d_set_color(g.labels[i], g.hovered == g.labels[i] ? g.gold : SK_COLOR_RAYWHITE);
    }
    const sk_pick_stats_t stats = sk_pick_get_stats();

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_render_begin_mode_3d();
    sk_shape_draw_grid(16, 1.0f, g.grey);
    sk_render_end_mode_3d();
    sk_scene_draw(g.scene);

    sk_text_draw_ex(g.font, "libsk text3d: text in 3D, shapes, picking", 12, 10, 20, SK_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "hover: %s   [P] cube pickable: %s",
             pick.hit ? (pick.handle == g.cube ? "cube" : pick.handle == g.sphere ? "sphere"
                         : pick.handle == g.panel ? "rectangle" : pick.handle == g.sign ? "sign" : "label")
                      : "nothing",
             sk_shape_is_pickable(g.cube) ? "yes" : "no");
    sk_text_draw_ex(g.font, line, 12, 36, 16, SK_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "pick stats: %d box tests (%d rejected), %d exact tests, %d hits",
             stats.broadphase_tests, stats.broadphase_rejects, stats.narrowphase_tests, stats.narrowphase_hits);
    sk_text_draw_ex(g.font, line, 12, 56, 16, SK_COLOR_LIGHTGRAY);
    sk_text_draw_fps_ex(g.font, 12, 80, 16, SK_COLOR_LIME);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1000, 640, "libsk text3d", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
