/* libsk scene example — retained shapes in a scene, drawn via sk_scene_draw().
 *
 * Click shapes to select them (sk_scene_pick). The selected shape turns white. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "sk.h"

static sk_handle_t g_scene;
static sk_handle_t g_camera;
static sk_handle_t g_bg;
static sk_handle_t g_spinner;
static sk_handle_t g_selected;
static sk_handle_t g_ring[8];
static sk_handle_t g_sphere;

static void on_init(void *user_data)
{
    (void)user_data;

    g_bg = sk_color_create(24, 26, 34, 255);
    g_camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g_camera, 16.0f, 11.0f, 16.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);

    for (int i = 0; i < 8; i++) {
        float a = (float)(i * 6.2831853 / 8.0);
        sk_handle_t cube = sk_shape_create();
        sk_shape_set_cube(cube, 1.5f, 1.5f, 1.5f);
        sk_shape_set_transform(cube, cosf(a) * 6.0f, 0.75f, sinf(a) * 6.0f,
                               0.0f, a, 0.0f, 1.0f, 1.0f, 1.0f);
        sk_shape_set_color(cube, i % 2 ? SK_COLOR_SKYBLUE : SK_COLOR_ORANGE);
        sk_scene_add(g_scene, cube, 0);
        g_ring[i] = cube;
    }

    g_sphere = sk_shape_create();
    sk_shape_set_sphere(g_sphere, 1.5f);
    sk_shape_set_transform(g_sphere, 0.0f, 2.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    sk_shape_set_color(g_sphere, SK_COLOR_GOLD);
    sk_scene_add(g_scene, g_sphere, 0);

    g_spinner = sk_shape_create();
    sk_shape_set_cube(g_spinner, 2.0f, 2.0f, 2.0f);
    sk_shape_set_color(g_spinner, SK_COLOR_LIME);
    sk_scene_add(g_scene, g_spinner, 1);

    sk_debug_enable_fps(12, 10, 16);
}

static sk_handle_t default_color_for(sk_handle_t shape)
{
    int i;

    if (shape == g_spinner) {
        return SK_COLOR_LIME;
    }
    if (shape == g_sphere) {
        return SK_COLOR_GOLD;
    }
    for (i = 0; i < 8; i++) {
        if (g_ring[i] == shape) {
            return (i % 2) ? SK_COLOR_SKYBLUE : SK_COLOR_ORANGE;
        }
    }
    return SK_COLOR_RAYWHITE;
}

static void update_selection(sk_handle_t hit)
{
    if (hit == g_selected) {
        return;
    }

    if (g_selected != 0) {
        sk_shape_set_color(g_selected, default_color_for(g_selected));
    }

    g_selected = hit;
    if (g_selected != 0) {
        sk_shape_set_color(g_selected, SK_COLOR_RAYWHITE);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)user_data;

    float t = (float)sk_get_time();
    sk_mouse_state_t mouse = sk_input_get_mouse_state();
    char status[96];

    sk_camera3d_set_view(g_camera, cosf(t * 0.35f) * 18.0f, 11.0f, sinf(t * 0.35f) * 18.0f,
                         0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    sk_shape_set_transform(g_spinner, 0.0f, 5.0f, 0.0f, t * 1.3f, t * 0.9f, 0.0f,
                           1.0f, 1.0f, 1.0f);

    if (mouse.left == SK_BUTTON_PRESSED) {
        sk_pick_result_t pick = sk_scene_pick(g_scene, 0, mouse.x, mouse.y);
        update_selection(pick.hit ? pick.handle : 0);
    }

    sk_render_begin();
    sk_render_clear_background(g_bg);

    sk_render_begin_mode_3d();
    sk_shape_draw_grid(24, 1.0f, SK_COLOR_DARKGRAY);
    sk_render_end_mode_3d();

    sk_scene_draw(g_scene);

    sk_text_draw("libsk + sokol — scene pick", 12, 36, 24, SK_COLOR_RAYWHITE);
    sk_text_draw("click a shape to select it", 12, 70, 16, SK_COLOR_LIGHTGRAY);

    if (g_selected != 0) {
        snprintf(status, sizeof(status), "selected handle: %u", (unsigned int)g_selected);
    } else {
        snprintf(status, sizeof(status), "selected: none");
    }
    sk_text_draw(status, 12, 94, 16, SK_COLOR_LIGHTGRAY);

    sk_render_end();

    if (sk_input_get_keyboard_state().keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(900, 700, "libsk scene3d", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
