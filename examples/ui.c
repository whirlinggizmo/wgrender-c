/* libwgrender ui example — pointer interaction with 2D and 3D members of one scene.
 *
 * The buttons, the progress bar and the scrolling list come from examples/ui_widgets.h,
 * which builds them out of 2D shapes, text2d and scene interaction — libwgrender has no
 * widget API (docs/ROADMAP.md, "GUI direction"), so this is how a game writes one.
 *
 *   - a nine-slice sprite is the panel behind the controls; presses on it don't orbit
 *   - buttons (rounded 2D shapes + centered text2d) react to hover and press;
 *     clicking counts and fills the progress bar
 *   - "Enable"/"Disable" toggles the third button: disabled, it still blocks the
 *     pointer but doesn't react
 *   - the note under the bar is wrapped text (wgr_text2d_set_max_width)
 *   - the list at the bottom is clipped to the panel (wgr_scene_set_clip): the mouse
 *     wheel scrolls it, and rows scrolled out of the box can't be hovered or clicked
 *   - the woman (a 3D member) lights up on hover; clicking it starts or stops its
 *     animation
 *   - dragging anywhere else orbits the camera; a drag that starts on a button
 *     doesn't (wgr_input_is_pointer_captured)
 *   - the header is immediate drawing, next to all that retained UI: the panel's
 *     nine-slice texture drawn directly, and a rounded, bordered status pill
 * Touch works like the mouse. ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"
#include "ui_widgets.h"

#define WOMAN_CASUAL_PATH "models/woman_casual/woman_casual.glb"
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
static const char *ROW_NAMES[ROWS] = {"Sponza", "Flight helmet", "Woman", "Damaged helmet",
                                      "Water bottle", "Lantern", "Sphere grid", "Boom box"};

static struct {
    wgr_handle_t scene, camera, sun;
    ui_theme_t theme;
    wgr_color_t bg, highlight, pill, pill_edge;
    wgr_handle_t panel, divider, note;
    ui_button_t buttons[BUTTONS];
    ui_bar_t bar;
    ui_list_t list;
    wgr_handle_t woman_casual;
    wgr_handle_t panel_texture;
    int clicks;
    bool animating;
    float yaw;
} g;

static void on_woman_casual(const char *path, void *user)
{
    const wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.woman_casual, mesh);
    wgr_mesh_release(mesh);
}

static void on_panel(const char *path, void *user)
{
    (void)user;
    g.panel_texture = wgr_texture_create(path); /* kept: the header draws it too */
    wgr_sprite2d_set_texture(g.panel, g.panel_texture);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void place_camera(void)
{
    wgr_camera3d_set_view(g.camera, sinf(g.yaw) * 5.0f, 1.6f, cosf(g.yaw) * 5.0f, 0, 0.9f, 0, 0, 1, 0);
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    g.theme = ui_theme_default();
    g.bg = wgr_color_rgba(30, 34, 44, 255);
    g.highlight = wgr_color_rgba(255, 220, 120, 255);
    g.pill = wgr_color_rgba(40, 46, 62, 230);
    g.pill_edge = wgr_color_rgba(90, 105, 140, 255);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    place_camera();
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, WGR_COLOR_WHITE, 0.35f);
    wgr_scene_set_interactive(g.scene, true);

    g.sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(g.sun, -0.4f, -1.0f, -0.6f);
    wgr_scene_add(g.scene, g.sun, 0);

    g.woman_casual = wgr_model_create(0);
    wgr_model_set_animation(g.woman_casual, 3);
    wgr_scene_add(g.scene, g.woman_casual, 0);

    /* the panel: one 48x48 texture with 16 px borders, stretched to any size */
    g.panel = wgr_sprite2d_create(0);
    wgr_sprite2d_set_nine_slice(g.panel, 16, 16, 16, 16);
    wgr_sprite2d_set_pivot(g.panel, 0, 0);
    wgr_sprite2d_set_position(g.panel, PANEL_X, PANEL_Y);
    wgr_sprite2d_set_size(g.panel, PANEL_WIDTH, PANEL_HEIGHT);
    wgr_scene_add(g.scene, g.panel, LAYER_PANEL); /* pickable, so presses on it don't orbit */

    g.divider = wgr_shape2d_create();
    wgr_shape2d_set_line(g.divider, 0, 0, 220, 0, 2);
    wgr_shape2d_set_transform(g.divider, 30, 300, 0, 1, 1);
    wgr_shape2d_set_color(g.divider, g.theme.disabled);
    wgr_shape2d_set_pickable(g.divider, false);
    wgr_scene_add(g.scene, g.divider, LAYER_CONTROL);

    g.bar = ui_bar_create(g.scene, LAYER_CONTROL, 30, 320, 220, 18);

    /* wrapped note: laid out inside 220 pixels, breaking between words */
    g.note = wgr_text2d_create(0);
    wgr_text2d_set_text(g.note, "Every click fills the bar. The list below is clipped to the panel: "
                               "scroll it with the wheel.");
    wgr_text2d_set_size(g.note, 14);
    wgr_text2d_set_max_width(g.note, 220);
    wgr_text2d_set_position(g.note, 30, 352);
    wgr_text2d_set_color(g.note, g.theme.text_disabled);
    wgr_text2d_set_pickable(g.note, false);
    wgr_scene_add(g.scene, g.note, LAYER_LABEL);

    for (int i = 0; i < BUTTONS; i++) {
        g.buttons[i] = ui_button_create(g.scene, LAYER_CONTROL, LABELS[i], 30, 100.0f + 60.0f * (float)i, 220, 44, 18);
    }
    /* the list clips its rows and their labels to its box (LAYER_ROW, LAYER_ROW + 1) */
    g.list = ui_list_create(g.scene, LAYER_ROW, ROW_NAMES, ROWS, LIST_X, LIST_Y, LIST_WIDTH, LIST_HEIGHT, ROW_HEIGHT, 15);

    wgr_asset_add_task(wgr_asset_ensure_async(WOMAN_CASUAL_PATH, NULL, WGR_ASSET_NONE), on_woman_casual, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(PANEL_PATH, NULL, WGR_ASSET_NONE), on_panel, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    const wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    const wgr_handle_t hovered = wgr_scene_get_hovered(g.scene);
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();

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
    wgr_model_set_tint(g.woman_casual, wgr_scene_get_hover(g.scene, g.woman_casual) >= WGR_BUTTON_PRESSED &&
                                         wgr_scene_get_hover(g.scene, g.woman_casual) != WGR_BUTTON_RELEASED
                                     ? g.highlight
                                     : WGR_COLOR_WHITE);
    if (wgr_scene_is_clicked(g.scene, g.woman_casual)) {
        g.animating = !g.animating;
    }
    if (g.animating) {
        wgr_model_animate(g.woman_casual, dt);
    }

    /* orbit, unless the press started on UI */
    if (mouse.buttons[0] == WGR_BUTTON_DOWN && !wgr_input_is_pointer_captured()) {
        g.yaw -= (float)mouse.dx * 0.01f;
        place_camera();
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    /* the header is immediate drawing, next to the retained panel below it: the same
       nine-slice texture as the panel, and a rounded, bordered pill for the status */
    wgr_texture_draw_nine_slice(g.panel_texture, 0, 0, 0, 0, 16, 16, 16, 16, 10, 6, 640, 62, WGR_COLOR_WHITE);
    wgr_shape2d_draw_rounded_rectangle(18, 40, 624, 22, 11, 11, 11, 11, g.pill);
    wgr_shape2d_draw_border(18, 40, 624, 22, 1, 1, 1, 1, 11, 11, 11, 11, g.pill_edge);
    wgr_text_draw("libwgrender ui: hover, press and click 2D and 3D members", 22, 15, 20, g.theme.text);
    snprintf(line, sizeof(line), "clicks: %d   selected: %s   hovered: %s   pointer captured: %s", g.clicks,
             selected < 0 ? "nothing" : ROW_NAMES[selected],
             hovered == 0 ? "nothing" : hovered == g.woman_casual ? "the woman" : hovered == g.panel ? "the panel" : "UI",
             wgr_input_is_pointer_captured() ? "yes" : "no");
    wgr_text_draw(line, 28, 43, 15, g.theme.text_disabled);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(960, 600, "libwgrender ui", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
