/* libsk ui example — pointer interaction with 2D and 3D members of one scene.
 *
 * The buttons, the progress bar and the scrolling list come from examples/ui_widgets.h,
 * which builds them out of 2D shapes, text2d and scene interaction — libsk has no
 * widget API (docs/ROADMAP.md, "GUI direction"), so this is how a game writes one.
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
 *   - the header is immediate drawing, next to all that retained UI: the panel's
 *     nine-slice texture drawn directly, and a rounded, bordered status pill
 * Touch works like the mouse. ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"
#include "ui_widgets.h"

#define GUMSHOE_PATH "models/gumshoe/gumshoe.glb"
#define PANEL_PATH "textures/ui_panel.png"

enum { BUTTONS = 3, ROWS = 8 };

/* Layers, bottom to top. Each widget puts its labels on the layer above the one it's
 * given (ui_widgets.h), so a control on LAYER_CONTROL labels on LAYER_LABEL, and the
 * list's rows and labels are clipped to the same box on LAYER_ROW and the one above. */
enum { LAYER_PANEL = 0, LAYER_CONTROL = 1, LAYER_LABEL = 2, LAYER_ROW = 5 };

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
    ui_theme_t theme;
    sk_color_t bg, highlight, pill, pill_edge;
    sk_handle_t panel, divider, note;
    ui_button_t buttons[BUTTONS];
    ui_bar_t bar;
    ui_list_t list;
    sk_handle_t gumshoe;
    sk_handle_t panel_texture;
    int clicks;
    bool animating;
    float yaw;
} g;

static void on_gumshoe(const char *path, void *user)
{
    const sk_handle_t mesh = sk_mesh_create(path);
    (void)user;
    sk_model_set_mesh(g.gumshoe, mesh);
    sk_mesh_release(mesh);
}

static void on_panel(const char *path, void *user)
{
    (void)user;
    g.panel_texture = sk_texture_create(path); /* kept: the header draws it too */
    sk_sprite2d_set_texture(g.panel, g.panel_texture);
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
    g.theme = ui_theme_default();
    g.bg = sk_color_rgba(30, 34, 44, 255);
    g.highlight = sk_color_rgba(255, 220, 120, 255);
    g.pill = sk_color_rgba(40, 46, 62, 230);
    g.pill_edge = sk_color_rgba(90, 105, 140, 255);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    place_camera();
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_ambient(g.scene, SK_COLOR_WHITE, 0.35f);
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

    g.divider = sk_shape2d_create();
    sk_shape2d_set_line(g.divider, 0, 0, 220, 0, 2);
    sk_shape2d_set_transform(g.divider, 30, 300, 0, 1, 1);
    sk_shape2d_set_color(g.divider, g.theme.disabled);
    sk_shape2d_set_pickable(g.divider, false);
    sk_scene_add(g.scene, g.divider, LAYER_CONTROL);

    g.bar = ui_bar_create(g.scene, LAYER_CONTROL, 30, 320, 220, 18);

    /* wrapped note: laid out inside 220 pixels, breaking between words */
    g.note = sk_text2d_create(0);
    sk_text2d_set_text(g.note, "Every click fills the bar. The list below is clipped to the panel: "
                               "scroll it with the wheel.");
    sk_text2d_set_size(g.note, 14);
    sk_text2d_set_max_width(g.note, 220);
    sk_text2d_set_position(g.note, 30, 352);
    sk_text2d_set_color(g.note, g.theme.text_disabled);
    sk_text2d_set_pickable(g.note, false);
    sk_scene_add(g.scene, g.note, LAYER_LABEL);

    for (int i = 0; i < BUTTONS; i++) {
        g.buttons[i] = ui_button_create(g.scene, LAYER_CONTROL, LABELS[i], 30, 100.0f + 60.0f * (float)i, 220, 44, 18);
    }
    /* the list clips its rows and their labels to its box (LAYER_ROW, LAYER_ROW + 1) */
    g.list = ui_list_create(g.scene, LAYER_ROW, ROW_NAMES, ROWS, LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT, ROW_HEIGHT, 15);

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

    /* buttons: each colors itself and says whether it was clicked */
    const bool counted = ui_button_update(&g.buttons[0], g.scene, &g.theme);
    const bool toggled = ui_button_update(&g.buttons[1], g.scene, &g.theme);
    const bool counted_too = ui_button_update(&g.buttons[2], g.scene, &g.theme);
    if (counted || counted_too) {
        g.clicks++;
    }
    if (toggled) {
        ui_button_set_enabled(&g.buttons[2], !ui_button_is_enabled(&g.buttons[2]));
    }
    ui_bar_set_value(&g.bar, (float)(g.clicks % 11) / 10.0f, &g.theme);

    /* the clipped list: the wheel scrolls it, clicking a row selects it */
    const int selected = ui_list_update(&g.list, g.scene, &g.theme, mouse.wheel);

    /* the 3D model */
    sk_model_set_tint(g.gumshoe, sk_scene_get_hover(g.scene, g.gumshoe) >= SK_BUTTON_PRESSED &&
                                         sk_scene_get_hover(g.scene, g.gumshoe) != SK_BUTTON_RELEASED
                                     ? g.highlight
                                     : SK_COLOR_WHITE);
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
    /* the header is immediate drawing, next to the retained panel below it: the same
       nine-slice texture as the panel, and a rounded, bordered pill for the status */
    sk_texture_draw_nine_slice(g.panel_texture, 0, 0, 0, 0, 16, 16, 16, 16, 10, 6, 640, 62, SK_COLOR_WHITE);
    sk_shape2d_draw_rounded_rectangle(18, 40, 624, 22, 11, 11, 11, 11, g.pill);
    sk_shape2d_draw_border(18, 40, 624, 22, 1, 1, 1, 1, 11, 11, 11, 11, g.pill_edge);
    sk_text_draw("libsk ui: hover, press and click 2D and 3D members", 22, 15, 20, g.theme.text);
    snprintf(line, sizeof(line), "clicks: %d   selected: %s   hovered: %s   pointer captured: %s", g.clicks,
             selected < 0 ? "nothing" : ROW_NAMES[selected],
             hovered == 0 ? "nothing" : hovered == g.gumshoe ? "gumshoe" : hovered == g.panel ? "the panel" : "UI",
             sk_input_is_pointer_captured() ? "yes" : "no");
    sk_text_draw(line, 28, 43, 15, g.theme.text_disabled);
    sk_render_end();
}

int main(void)
{
    sk_init_values(960, 600, "libsk ui", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
