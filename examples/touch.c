/* libwgrender touch example — fingers and the two-finger gesture.
 *
 *   - every finger gets a numbered ring (its id) while it's down, and a fading one
 *     where it lifted;
 *   - two fingers pan, pinch and twist the logo (wgr_input_get_touch_gesture), about
 *     the point between them, so it stays under your fingers;
 *   - one finger is also the pointer: drag the coin. A second finger cancels that
 *     drag (the pointer is released off-screen), so a pinch never drops the coin
 *     somewhere or clicks anything.
 * Without a touch screen: drag with the mouse, and the wheel zooms the logo.
 * ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define TILES_PATH "textures/tiles.png"
#define RING 38.0f

static struct {
    wgr_handle_t logo, tile;
    float logo_x, logo_y, logo_scale, logo_rotation;
    float tile_x, tile_y;
    bool dragging;
    float lifted[WGR_INPUT_MAX_TOUCHES][3]; /* x, y, fade (1 -> 0) of a lifted finger */
    wgr_color_t colors[WGR_INPUT_MAX_TOUCHES];
} g;

static void on_logo(const char *path, void *user)
{
    (void)user;
    g.logo = wgr_sprite2d_create(wgr_texture_create(path));
    wgr_sprite2d_set_size(g.logo, 240, 240);
}

static void on_tiles(const char *path, void *user)
{
    const wgr_handle_t texture = wgr_texture_create(path);
    (void)user;
    wgr_texture_set_sampling(texture, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_NEAREST);
    g.tile = wgr_sprite2d_create(texture);
    wgr_sprite2d_set_source(g.tile, 32, 16, 16, 16); /* the coin */
    wgr_sprite2d_set_size(g.tile, 96, 96);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    const vec2_t screen = wgr_window_get_screen_size();
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_add_task(wgr_asset_ensure_async(LOGO_PATH, NULL, WGR_ASSET_NONE), on_logo, on_failed, NULL);
    wgr_asset_add_task(wgr_asset_ensure_async(TILES_PATH, NULL, WGR_ASSET_NONE), on_tiles, on_failed, NULL);
    g.logo_x = screen.x * 0.5f;
    g.logo_y = screen.y * 0.45f;
    g.logo_scale = 1.0f;
    g.tile_x = screen.x * 0.5f;
    g.tile_y = screen.y * 0.8f;
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        g.colors[i] = wgr_color_rgba(90 + 20 * i, 200 - 15 * i, 120 + 17 * i, 255);
    }
}

/* Scale and turn the logo about (x, y), so the point under the fingers stays put. */
static void transform_logo(float x, float y, float scale, float rotation)
{
    const float c = cosf(rotation), s = sinf(rotation);
    const float ox = (g.logo_x - x) * scale, oy = (g.logo_y - y) * scale;
    g.logo_x = x + ox * c - oy * s;
    g.logo_y = y + ox * s + oy * c;
    g.logo_scale = fminf(fmaxf(g.logo_scale * scale, 0.2f), 8.0f);
    g.logo_rotation += rotation;
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_mouse_state_t mouse = wgr_input_get_mouse_state();
    const wgr_touch_gesture_t gesture = wgr_input_get_touch_gesture();
    const int count = wgr_input_get_touch_count();
    char line[96];
    (void)tick_fraction;
    (void)user_data;

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) wgr_request_quit();

    /* two fingers move the logo; the wheel zooms it about the mouse */
    if (gesture.active) {
        g.logo_x += gesture.dx;
        g.logo_y += gesture.dy;
        transform_logo(gesture.x, gesture.y, gesture.scale, gesture.rotation);
    }
    if (mouse.wheel != 0.0f) {
        transform_logo((float)mouse.x, (float)mouse.y, powf(1.1f, mouse.wheel), 0.0f);
    }

    /* the pointer (mouse, or one finger) drags the coin */
    if (mouse.left == WGR_BUTTON_PRESSED && fabsf((float)mouse.x - g.tile_x) < 48 &&
        fabsf((float)mouse.y - g.tile_y) < 48) {
        g.dragging = true;
    } else if (mouse.left == WGR_BUTTON_RELEASED || mouse.left == WGR_BUTTON_UP) {
        g.dragging = false;
    }
    if (g.dragging) {
        g.tile_x = (float)mouse.x;
        g.tile_y = (float)mouse.y;
    }

    /* where fingers lifted: a ring that fades */
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        g.lifted[i][2] = fmaxf(g.lifted[i][2] - dt * 2.0f, 0.0f);
    }
    for (int i = 0; i < count; i++) {
        const wgr_touch_t touch = wgr_input_get_touch(i);
        if (touch.state == WGR_BUTTON_RELEASED) {
            g.lifted[touch.id][0] = touch.x;
            g.lifted[touch.id][1] = touch.y;
            g.lifted[touch.id][2] = 1.0f;
        }
    }

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(22, 25, 33, 255));
    if (g.logo != 0) {
        wgr_sprite2d_set_position(g.logo, g.logo_x, g.logo_y);
        wgr_sprite2d_set_scale(g.logo, g.logo_scale, g.logo_scale);
        wgr_sprite2d_set_rotation(g.logo, g.logo_rotation);
        wgr_sprite2d_draw(g.logo);
    }
    if (g.tile != 0) {
        wgr_sprite2d_set_position(g.tile, g.tile_x, g.tile_y);
        wgr_sprite2d_set_tint(g.tile, g.dragging ? wgr_color_rgba(255, 230, 150, 255) : WGR_COLOR_WHITE);
        wgr_sprite2d_draw(g.tile);
    }
    for (int i = 0; i < WGR_INPUT_MAX_TOUCHES; i++) {
        if (g.lifted[i][2] > 0.0f) {
            wgr_shape2d_draw_circle_lines(g.lifted[i][0], g.lifted[i][1], RING * (2.0f - g.lifted[i][2]),
                                         wgr_color_with_alpha(g.colors[i], (int)(200 * g.lifted[i][2])));
        }
    }
    for (int i = 0; i < count; i++) {
        const wgr_touch_t touch = wgr_input_get_touch(i);
        if (touch.state == WGR_BUTTON_RELEASED) continue;
        wgr_shape2d_draw_circle(touch.x, touch.y, RING, wgr_color_with_alpha(g.colors[touch.id], 90));
        wgr_shape2d_draw_circle_lines(touch.x, touch.y, RING, g.colors[touch.id]);
        snprintf(line, sizeof(line), "%d", touch.id);
        wgr_text_draw_ex(0, line, touch.x - 6, touch.y - RING - 26, 22, g.colors[touch.id]);
    }
    if (gesture.active) {
        wgr_shape2d_draw_circle(gesture.x, gesture.y, 6, WGR_COLOR_WHITE);
    }

    snprintf(line, sizeof(line), "fingers: %d   pointer: %s", count,
             mouse.left == WGR_BUTTON_DOWN || mouse.left == WGR_BUTTON_PRESSED ? "down" : "up");
    wgr_text_draw_ex(0, line, 16, 16, 18, WGR_COLOR_WHITE);
    snprintf(line, sizeof(line), "logo: scale %.2f, turn %.0f deg", g.logo_scale, g.logo_rotation * 57.29578f);
    wgr_text_draw_ex(0, line, 16, 40, 18, WGR_COLOR_WHITE);
    wgr_text_draw_ex(0, "two fingers: pan, pinch, twist the logo; one finger drags the coin", 16, 64, 16,
                    wgr_color_rgba(150, 158, 175, 255));
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(900, 700, "libwgrender touch", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
