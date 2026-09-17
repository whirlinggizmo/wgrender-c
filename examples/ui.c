/* libsk ui example — pointer interaction with 2D and 3D members of one scene.
 *
 *   - a rounded panel (2D shapes) holds the controls; presses on it don't orbit
 *   - buttons (rounded 2D shapes + text2d) react to hover and press; clicking counts
 *     and fills the progress bar
 *   - "Enable"/"Disable" toggles the third button: disabled, it still blocks the
 *     pointer but doesn't react
 *   - the gumshoe (a 3D member) lights up on hover; clicking it starts or stops its
 *     animation
 *   - dragging anywhere else orbits the camera; a drag that starts on a button
 *     doesn't (sk_input_is_pointer_captured)
 * Touch works like the mouse. ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"

enum { BUTTONS = 3 };

static const char *LABELS[BUTTONS] = {"Count", "Enable / Disable", "Count too"};

static struct {
    sk_handle_t scene, camera, sun;
    sk_handle_t bg, idle, hover, pressed, disabled, text, text_disabled, highlight, panel_color, outline, bar_color;
    sk_handle_t panel, divider, bar_back, bar_fill, bar_tip;
    sk_handle_t buttons[BUTTONS], labels[BUTTONS];
    sk_handle_t gumshoe;
    int clicks;
    bool animating;
    float yaw;
} g;

static void on_gumshoe(const char *path, void *user)
{
    const sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_destroy(mesh);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void place_camera(void)
{
    sk_camera3d_set_view(g.camera, sinf(g.yaw) * 5.0f, 1.6f, cosf(g.yaw) * 5.0f, 0, 0.9f, 0, 0, 1, 0);
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_create(30, 34, 44, 255);
    g.idle = sk_color_create(70, 80, 105, 255);
    g.hover = sk_color_create(95, 115, 160, 255);
    g.pressed = sk_color_create(45, 55, 80, 255);
    g.disabled = sk_color_create(55, 58, 64, 255);
    g.text = sk_color_create(235, 238, 245, 255);
    g.text_disabled = sk_color_create(120, 124, 132, 255);
    g.highlight = sk_color_create(255, 220, 120, 255);
    g.panel_color = sk_color_create(16, 18, 26, 200);
    g.outline = sk_color_create(150, 175, 230, 255);
    g.bar_color = sk_color_create(110, 200, 140, 255);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    place_camera();
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, 0, 0.35f);
    sk_scene_set_interactive(g.scene, true);

    g.sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(g.sun, -0.4f, -1.0f, -0.6f);
    sk_scene_add(g.scene, g.sun, 0);

    g.gumshoe = sk_model_create(0);
    sk_model_set_animation(g.gumshoe, 3);
    sk_scene_add(g.scene, g.gumshoe, 0);

    g.panel = sk_shape_create(); /* behind the controls; pickable, so presses on it don't orbit */
    sk_shape_set_rectangle_2d(g.panel, 260, 330, 14);
    sk_shape_set_transform_2d(g.panel, 10, 80, 0, 1, 1);
    sk_shape_set_color(g.panel, g.panel_color);
    sk_scene_add(g.scene, g.panel, 0);

    g.divider = sk_shape_create();
    sk_shape_set_line_2d(g.divider, 0, 0, 220, 0, 2);
    sk_shape_set_transform_2d(g.divider, 30, 330, 0, 1, 1);
    sk_shape_set_color(g.divider, g.disabled);
    sk_shape_set_pickable(g.divider, false);
    sk_scene_add(g.scene, g.divider, 1);

    g.bar_back = sk_shape_create();
    sk_shape_set_rectangle_2d(g.bar_back, 220, 18, 9);
    sk_shape_set_transform_2d(g.bar_back, 30, 360, 0, 1, 1);
    sk_shape_set_outline(g.bar_back, 2);
    sk_shape_set_color(g.bar_back, g.outline);
    sk_shape_set_pickable(g.bar_back, false);
    sk_scene_add(g.scene, g.bar_back, 2);
    g.bar_fill = sk_shape_create();
    sk_shape_set_color(g.bar_fill, g.bar_color);
    sk_shape_set_pickable(g.bar_fill, false);
    sk_scene_add(g.scene, g.bar_fill, 1);
    g.bar_tip = sk_shape_create();
    sk_shape_set_circle_2d(g.bar_tip, 6);
    sk_shape_set_color(g.bar_tip, g.text);
    sk_shape_set_pickable(g.bar_tip, false);
    sk_scene_add(g.scene, g.bar_tip, 2);

    for (int i = 0; i < BUTTONS; i++) {
        const float y = 100.0f + 60.0f * (float)i;
        g.buttons[i] = sk_shape_create();
        sk_shape_set_rectangle_2d(g.buttons[i], 220, 44, 10);
        sk_shape_set_transform_2d(g.buttons[i], 30, y, 0, 1, 1);
        sk_scene_add(g.scene, g.buttons[i], 1);

        g.labels[i] = sk_text2d_create(0);
        sk_text2d_set_text(g.labels[i], LABELS[i]);
        sk_text2d_set_size(g.labels[i], 18);
        sk_text2d_set_position(g.labels[i], 46, y + 12);
        sk_text2d_set_pickable(g.labels[i], false); /* the button under it takes the pointer */
        sk_scene_add(g.scene, g.labels[i], 2);
    }
    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_gumshoe, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    const sk_handle_t hovered = sk_scene_get_hovered(g.scene);
    char line[128];

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();

    /* buttons: color from their interaction state */
    for (int i = 0; i < BUTTONS; i++) {
        const bool enabled = sk_shape_is_enabled(g.buttons[i]);
        const sk_button_state_t press = sk_scene_get_press(g.scene, g.buttons[i]);
        const sk_button_state_t hover = sk_scene_get_hover(g.scene, g.buttons[i]);
        const bool held = press == SK_BUTTON_PRESSED || press == SK_BUTTON_DOWN;
        const bool over = hover == SK_BUTTON_PRESSED || hover == SK_BUTTON_DOWN;
        sk_shape_set_color(g.buttons[i], !enabled ? g.disabled : held ? g.pressed : over ? g.hover : g.idle);
        sk_text2d_set_color(g.labels[i], enabled ? g.text : g.text_disabled);
    }
    if (sk_scene_is_clicked(g.scene, g.buttons[0]) || sk_scene_is_clicked(g.scene, g.buttons[2])) {
        g.clicks++;
    }
    if (sk_scene_is_clicked(g.scene, g.buttons[1])) {
        sk_shape_set_enabled(g.buttons[2], !sk_shape_is_enabled(g.buttons[2]));
    }
    {
        const float fill = 220.0f * (float)(g.clicks % 11) / 10.0f;
        sk_shape_set_rectangle_2d(g.bar_fill, fill > 18.0f ? fill : 18.0f, 18, 9);
        sk_shape_set_transform_2d(g.bar_fill, 30, 360, 0, 1, 1);
        sk_shape_set_visible(g.bar_fill, g.clicks % 11 > 0);
        sk_shape_set_transform_2d(g.bar_tip, 30 + (fill > 9.0f ? fill - 9.0f : 9.0f), 369, 0, 1, 1);
    }

    /* the 3D model */
    sk_model_set_tint(g.gumshoe, sk_scene_get_hover(g.scene, g.gumshoe) >= SK_BUTTON_PRESSED &&
                                         sk_scene_get_hover(g.scene, g.gumshoe) != SK_BUTTON_RELEASED
                                     ? g.highlight
                                     : 0);
    if (sk_scene_is_clicked(g.scene, g.gumshoe)) {
        g.animating = !g.animating;
    }
    if (g.animating) {
        sk_model_animate(g.gumshoe, dt);
    }

    /* orbit, unless the press started on UI */
    if (mouse.buttons[0] == SK_BUTTON_DOWN && !sk_input_is_pointer_captured()) {
        g.yaw -= (float)mouse.dx * 0.01f;
        place_camera();
    }

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_scene_draw(g.scene);
    sk_text_draw("libsk ui: hover, press and click 2D and 3D members", 20, 20, 20, g.text);
    snprintf(line, sizeof(line), "clicks: %d   hovered: %s   pointer captured: %s", g.clicks,
             hovered == 0 ? "nothing" : hovered == g.gumshoe ? "gumshoe" : hovered == g.panel ? "the panel" : "a button",
             sk_input_is_pointer_captured() ? "yes" : "no");
    sk_text_draw(line, 20, 50, 16, g.text_disabled);
    sk_render_end();
}

int main(void)
{
    sk_init_values(960, 600, "libsk ui", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
