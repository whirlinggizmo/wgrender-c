/* libsk touch example — fingers and the two-finger gesture.
 *
 *   - every finger gets a numbered ring (its id) while it's down, and a fading one
 *     where it lifted;
 *   - two fingers pan, pinch and twist the logo (sk_input_get_touch_gesture), about
 *     the point between them, so it stays under your fingers;
 *   - one finger is also the pointer: drag the coin. A second finger cancels that
 *     drag (the pointer is released off-screen), so a pinch never drops the coin
 *     somewhere or clicks anything.
 * Without a touch screen: drag with the mouse, and the wheel zooms the logo.
 * ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define LOGO_PATH "sprites/logo/wg-logo-white-alpha.png"
#define TILES_PATH "textures/tiles.png"
#define RING 38.0f

static struct {
    sk_handle_t logo, tile;
    float logo_x, logo_y, logo_scale, logo_rotation;
    float tile_x, tile_y;
    bool dragging;
    float lifted[SK_INPUT_MAX_TOUCHES][3]; /* x, y, fade (1 -> 0) of a lifted finger */
    sk_color_t colors[SK_INPUT_MAX_TOUCHES];
} g;

static void on_logo(const char *path, void *user)
{
    (void)user;
    g.logo = sk_sprite2d_create(sk_texture_create(path));
    sk_sprite2d_set_size(g.logo, 240, 240);
}

static void on_tiles(const char *path, void *user)
{
    const sk_handle_t texture = sk_texture_create(path);
    (void)user;
    sk_texture_set_sampling(texture, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_NEAREST);
    g.tile = sk_sprite2d_create(texture);
    sk_sprite2d_set_source(g.tile, 32, 16, 16, 16); /* the coin */
    sk_sprite2d_set_size(g.tile, 96, 96);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    const vec2_t screen = sk_window_get_screen_size();
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    sk_asset_add_task(sk_asset_ensure_async(LOGO_PATH, NULL, SK_ASSET_NONE), on_logo, on_failed, NULL);
    sk_asset_add_task(sk_asset_ensure_async(TILES_PATH, NULL, SK_ASSET_NONE), on_tiles, on_failed, NULL);
    g.logo_x = screen.x * 0.5f;
    g.logo_y = screen.y * 0.45f;
    g.logo_scale = 1.0f;
    g.tile_x = screen.x * 0.5f;
    g.tile_y = screen.y * 0.8f;
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        g.colors[i] = sk_color_rgba(90 + 20 * i, 200 - 15 * i, 120 + 17 * i, 255);
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
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    const sk_touch_gesture_t gesture = sk_input_get_touch_gesture();
    const int count = sk_input_get_touch_count();
    char line[96];
    (void)tick_fraction;
    (void)user_data;

    if (sk_input_get_keyboard_state().keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();

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
    if (mouse.left == SK_BUTTON_PRESSED && fabsf((float)mouse.x - g.tile_x) < 48 &&
        fabsf((float)mouse.y - g.tile_y) < 48) {
        g.dragging = true;
    } else if (mouse.left == SK_BUTTON_RELEASED || mouse.left == SK_BUTTON_UP) {
        g.dragging = false;
    }
    if (g.dragging) {
        g.tile_x = (float)mouse.x;
        g.tile_y = (float)mouse.y;
    }

    /* where fingers lifted: a ring that fades */
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        g.lifted[i][2] = fmaxf(g.lifted[i][2] - dt * 2.0f, 0.0f);
    }
    for (int i = 0; i < count; i++) {
        const sk_touch_t touch = sk_input_get_touch(i);
        if (touch.state == SK_BUTTON_RELEASED) {
            g.lifted[touch.id][0] = touch.x;
            g.lifted[touch.id][1] = touch.y;
            g.lifted[touch.id][2] = 1.0f;
        }
    }

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(22, 25, 33, 255));
    if (g.logo != 0) {
        sk_sprite2d_set_position(g.logo, g.logo_x, g.logo_y);
        sk_sprite2d_set_scale(g.logo, g.logo_scale, g.logo_scale);
        sk_sprite2d_set_rotation(g.logo, g.logo_rotation);
        sk_sprite2d_draw(g.logo);
    }
    if (g.tile != 0) {
        sk_sprite2d_set_position(g.tile, g.tile_x, g.tile_y);
        sk_sprite2d_set_tint(g.tile, g.dragging ? sk_color_rgba(255, 230, 150, 255) : SK_COLOR_WHITE);
        sk_sprite2d_draw(g.tile);
    }
    for (int i = 0; i < SK_INPUT_MAX_TOUCHES; i++) {
        if (g.lifted[i][2] > 0.0f) {
            sk_shape2d_draw_circle_lines(g.lifted[i][0], g.lifted[i][1], RING * (2.0f - g.lifted[i][2]),
                                         sk_color_with_alpha(g.colors[i], (int)(200 * g.lifted[i][2])));
        }
    }
    for (int i = 0; i < count; i++) {
        const sk_touch_t touch = sk_input_get_touch(i);
        if (touch.state == SK_BUTTON_RELEASED) continue;
        sk_shape2d_draw_circle(touch.x, touch.y, RING, sk_color_with_alpha(g.colors[touch.id], 90));
        sk_shape2d_draw_circle_lines(touch.x, touch.y, RING, g.colors[touch.id]);
        snprintf(line, sizeof(line), "%d", touch.id);
        sk_text_draw_ex(0, line, touch.x - 6, touch.y - RING - 26, 22, g.colors[touch.id]);
    }
    if (gesture.active) {
        sk_shape2d_draw_circle(gesture.x, gesture.y, 6, SK_COLOR_WHITE);
    }

    snprintf(line, sizeof(line), "fingers: %d   pointer: %s", count,
             mouse.left == SK_BUTTON_DOWN || mouse.left == SK_BUTTON_PRESSED ? "down" : "up");
    sk_text_draw_ex(0, line, 16, 16, 18, SK_COLOR_WHITE);
    snprintf(line, sizeof(line), "logo: scale %.2f, turn %.0f deg", g.logo_scale, g.logo_rotation * 57.29578f);
    sk_text_draw_ex(0, line, 16, 40, 18, SK_COLOR_WHITE);
    sk_text_draw_ex(0, "two fingers: pan, pinch, twist the logo; one finger drags the coin", 16, 64, 16,
                    sk_color_rgba(150, 158, 175, 255));
    sk_render_end();
}

int main(void)
{
    sk_init_values(900, 700, "libsk touch", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_HIGHDPI);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
