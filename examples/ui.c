/* libsk ui example — pointer interaction with 2D and 3D members of one scene.
 *
 *   - a nine-slice sprite is the panel behind the controls; presses on it don't orbit
 *   - buttons (rounded 2D shapes + centered text2d) react to hover and press;
 *     clicking counts and fills the progress bar
 *   - "Enable"/"Disable" toggles the third button: disabled, it still blocks the
 *     pointer but doesn't react
 *   - the note under the bar is wrapped text (sk_text2d_set_max_width)
 *   - the list at the bottom is clipped to the panel (sk_scene_set_clip): the mouse
 *     wheel scrolls it, and rows scrolled out of the box can't be hovered or clicked
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
#define PANEL_PATH "textures/ui_panel.png"

enum { BUTTONS = 3, ROWS = 8 };

/* layers: the list's rows and their labels are clipped to the same box */
enum { LAYER_PANEL = 0, LAYER_CONTROL = 1, LAYER_LABEL = 2, LAYER_ROW = 5, LAYER_ROW_LABEL = 6 };

#define PANEL_X 10.0f
#define PANEL_Y 70.0f
#define PANEL_WIDTH 280.0f
#define PANEL_HEIGHT 470.0f
#define LIST_X 30.0f
#define LIST_Y 400.0f
#define LIST_WIDTH 240.0f
#define LIST_HEIGHT 120.0f
#define ROW_HEIGHT 34.0f

static const char *LABELS[BUTTONS] = {"Count", "Enable / Disable", "Count too"};
static const char *ROW_NAMES[ROWS] = {"Sponza", "Flight helmet", "Gumshoe", "Damaged helmet",
                                      "Water bottle", "Lantern", "Sphere grid", "Boom box"};

static struct {
    sk_handle_t scene, camera, sun;
    sk_handle_t bg, idle, hover, pressed, disabled, text, text_disabled, highlight, outline, bar_color, row_color,
        row_selected;
    sk_handle_t panel, divider, bar_back, bar_fill, bar_tip, note;
    sk_handle_t buttons[BUTTONS], labels[BUTTONS];
    sk_handle_t rows[ROWS], row_labels[ROWS];
    sk_handle_t gumshoe;
    int clicks;
    int selected;
    float scroll; /* pixels the list is scrolled down by */
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

static void on_panel(const char *path, void *user)
{
    const sk_handle_t texture = sk_texture_create(path);
    (void)user;
    sk_sprite2d_set_texture(g.panel, texture);
    sk_texture_destroy(texture);
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

/* Rows follow the scroll offset; the clip rectangle hides what leaves the box. */
static void place_rows(void)
{
    for (int i = 0; i < ROWS; i++) {
        const float y = LIST_Y + (float)i * ROW_HEIGHT - g.scroll;
        sk_shape_set_transform_2d(g.rows[i], LIST_X, y, 0, 1, 1);
        sk_text2d_set_position(g.row_labels[i], LIST_X + 12, y + ROW_HEIGHT * 0.5f);
    }
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
    g.outline = sk_color_create(150, 175, 230, 255);
    g.bar_color = sk_color_create(110, 200, 140, 255);
    g.row_color = sk_color_create(44, 50, 66, 255);
    g.row_selected = sk_color_create(80, 110, 90, 255);
    g.selected = -1;

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

    /* the panel: one 48x48 texture with 16 px borders, stretched to any size */
    g.panel = sk_sprite2d_create(0);
    sk_sprite2d_set_nine_slice(g.panel, 16, 16, 16, 16);
    sk_sprite2d_set_pivot(g.panel, 0, 0);
    sk_sprite2d_set_position(g.panel, PANEL_X, PANEL_Y);
    sk_sprite2d_set_size(g.panel, PANEL_WIDTH, PANEL_HEIGHT);
    sk_scene_add(g.scene, g.panel, LAYER_PANEL); /* pickable, so presses on it don't orbit */

    g.divider = sk_shape_create();
    sk_shape_set_line_2d(g.divider, 0, 0, 220, 0, 2);
    sk_shape_set_transform_2d(g.divider, 30, 300, 0, 1, 1);
    sk_shape_set_color(g.divider, g.disabled);
    sk_shape_set_pickable(g.divider, false);
    sk_scene_add(g.scene, g.divider, LAYER_CONTROL);

    g.bar_back = sk_shape_create();
    sk_shape_set_rectangle_2d(g.bar_back, 220, 18, 9);
    sk_shape_set_transform_2d(g.bar_back, 30, 320, 0, 1, 1);
    sk_shape_set_outline(g.bar_back, 2);
    sk_shape_set_color(g.bar_back, g.outline);
    sk_shape_set_pickable(g.bar_back, false);
    sk_scene_add(g.scene, g.bar_back, LAYER_LABEL);
    g.bar_fill = sk_shape_create();
    sk_shape_set_color(g.bar_fill, g.bar_color);
    sk_shape_set_pickable(g.bar_fill, false);
    sk_scene_add(g.scene, g.bar_fill, LAYER_CONTROL);
    g.bar_tip = sk_shape_create();
    sk_shape_set_circle_2d(g.bar_tip, 6);
    sk_shape_set_color(g.bar_tip, g.text);
    sk_shape_set_pickable(g.bar_tip, false);
    sk_scene_add(g.scene, g.bar_tip, LAYER_LABEL);

    /* wrapped note: laid out inside 220 pixels, breaking between words */
    g.note = sk_text2d_create(0);
    sk_text2d_set_text(g.note, "Every click fills the bar. The list below is clipped to the panel: "
                               "scroll it with the wheel.");
    sk_text2d_set_size(g.note, 14);
    sk_text2d_set_max_width(g.note, 220);
    sk_text2d_set_position(g.note, 30, 352);
    sk_text2d_set_color(g.note, g.text_disabled);
    sk_text2d_set_pickable(g.note, false);
    sk_scene_add(g.scene, g.note, LAYER_LABEL);

    for (int i = 0; i < BUTTONS; i++) {
        const float y = 100.0f + 60.0f * (float)i;
        g.buttons[i] = sk_shape_create();
        sk_shape_set_rectangle_2d(g.buttons[i], 220, 44, 10);
        sk_shape_set_transform_2d(g.buttons[i], 30, y, 0, 1, 1);
        sk_scene_add(g.scene, g.buttons[i], LAYER_CONTROL);

        /* centered on the button, so the label needs no measuring */
        g.labels[i] = sk_text2d_create(0);
        sk_text2d_set_text(g.labels[i], LABELS[i]);
        sk_text2d_set_size(g.labels[i], 18);
        sk_text2d_set_align(g.labels[i], SK_TEXT_ALIGN_CENTER, SK_TEXT_ALIGN_MIDDLE);
        sk_text2d_set_position(g.labels[i], 30 + 110, y + 22);
        sk_text2d_set_pickable(g.labels[i], false); /* the button under it takes the pointer */
        sk_scene_add(g.scene, g.labels[i], LAYER_LABEL);
    }

    /* the clipped list: rows and their labels live on two layers with the same box */
    for (int i = 0; i < ROWS; i++) {
        g.rows[i] = sk_shape_create();
        sk_shape_set_rectangle_2d(g.rows[i], LIST_WIDTH, ROW_HEIGHT - 4.0f, 6);
        sk_shape_set_color(g.rows[i], g.row_color);
        sk_scene_add(g.scene, g.rows[i], LAYER_ROW);

        g.row_labels[i] = sk_text2d_create(0);
        sk_text2d_set_text(g.row_labels[i], ROW_NAMES[i]);
        sk_text2d_set_size(g.row_labels[i], 15);
        sk_text2d_set_align(g.row_labels[i], SK_TEXT_ALIGN_LEFT, SK_TEXT_ALIGN_MIDDLE);
        sk_text2d_set_color(g.row_labels[i], g.text);
        sk_text2d_set_pickable(g.row_labels[i], false);
        sk_scene_add(g.scene, g.row_labels[i], LAYER_ROW_LABEL);
    }
    sk_scene_set_clip(g.scene, LAYER_ROW, LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT);
    sk_scene_set_clip(g.scene, LAYER_ROW_LABEL, LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT);
    place_rows();

    sk_asset_add_task(sk_asset_ensure_async(GUMSHOE_PATH, NULL, SK_ASSET_NONE), on_gumshoe, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(PANEL_PATH, NULL, SK_ASSET_NONE), on_panel, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    const sk_handle_t hovered = sk_scene_get_hovered(g.scene);
    char line[160];

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
        sk_shape_set_transform_2d(g.bar_fill, 30, 320, 0, 1, 1);
        sk_shape_set_visible(g.bar_fill, g.clicks % 11 > 0);
        sk_shape_set_transform_2d(g.bar_tip, 30 + (fill > 9.0f ? fill - 9.0f : 9.0f), 329, 0, 1, 1);
    }

    /* the clipped list: the wheel scrolls it, clicking a row selects it */
    {
        const float max_scroll = (float)ROWS * ROW_HEIGHT - LIST_HEIGHT;
        if (mouse.wheel != 0) {
            g.scroll -= (float)mouse.wheel * ROW_HEIGHT;
            g.scroll = g.scroll < 0.0f ? 0.0f : g.scroll > max_scroll ? max_scroll : g.scroll;
        }
        place_rows();
        for (int i = 0; i < ROWS; i++) {
            const sk_button_state_t hover = sk_scene_get_hover(g.scene, g.rows[i]);
            const bool over = hover == SK_BUTTON_PRESSED || hover == SK_BUTTON_DOWN;
            if (sk_scene_is_clicked(g.scene, g.rows[i])) {
                g.selected = i;
            }
            sk_shape_set_color(g.rows[i], g.selected == i ? g.row_selected : over ? g.hover : g.row_color);
        }
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
    snprintf(line, sizeof(line), "clicks: %d   selected: %s   hovered: %s   pointer captured: %s", g.clicks,
             g.selected < 0 ? "nothing" : ROW_NAMES[g.selected],
             hovered == 0 ? "nothing" : hovered == g.gumshoe ? "gumshoe" : hovered == g.panel ? "the panel" : "UI",
             sk_input_is_pointer_captured() ? "yes" : "no");
    sk_text_draw(line, 20, 46, 15, g.text_disabled);
    sk_render_end();
}

int main(void)
{
    sk_init_values(960, 600, "libsk ui", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
