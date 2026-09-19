/* libsk 2d example — a 2D world on an orthographic camera.
 *
 * libsk has no camera2d: a scrolling, zooming 2D world is sprite3d objects in the
 * XY plane (SK_SPRITE3D_FACING_FREE) under an orthographic camera3d, so it keeps
 * the same scenes, layers, depth order and ray picking as 3D.
 *
 *   - one sprite sheet (textures/tiles.png), one sprite3d per cell, cut out with
 *     sk_sprite3d_set_source
 *   - trees and flags are 16x32 in the sheet, drawn 1x2 world units with
 *     sk_sprite3d_set_extent, and stand on their cell because their pivot is the
 *     bottom edge (sk_sprite3d_set_pivot(0.5, 1))
 *   - drag or use the arrow keys to scroll, the wheel to zoom (the camera's
 *     orthographic height)
 *   - the sheet is pixel art (alpha 0 or 1): ground tiles are SK_ALPHA_OPAQUE, props
 *     SK_ALPHA_MASK, so nothing needs sorting and each layer draws in one batch
 *   - coins react to the pointer through the scene's interaction state: hovering
 *     lights them up, clicking collects them. Their picks are alpha-tested, so the
 *     transparent corners of a coin let the tile behind it take the click.
 * ESC quits. */
#include <math.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define TILES_PATH "textures/tiles.png"

enum { WORLD_W = 24, WORLD_H = 16, MAX_PROPS = 64 };
enum { LAYER_GROUND = 0, LAYER_PROPS = 1 };

/* cells of the sheet, in texture pixels: x, y, width, height */
static const float GRASS[4] = {0, 0, 16, 16};
static const float SAND[4] = {16, 0, 16, 16};
static const float WATER[4] = {32, 0, 16, 16};
static const float STONE[4] = {48, 0, 16, 16};
static const float TREE[4] = {0, 16, 16, 32};
static const float FLAG[4] = {16, 16, 16, 32};
static const float COIN[4] = {32, 16, 16, 16};
static const float ROCK[4] = {48, 16, 16, 16};

static struct {
    sk_handle_t scene, camera, texture;
    sk_color_t bg, shade, text, dim, highlight;
    sk_handle_t ground[WORLD_W * WORLD_H];
    sk_handle_t props[MAX_PROPS];
    bool is_coin[MAX_PROPS];
    int prop_count;
    int collected;
    float center_x, center_y; /* what the camera looks at, in world units */
    float zoom;               /* world units visible vertically */
    bool loaded;
} g;

static void place_camera(void)
{
    sk_camera3d_set_view(g.camera, g.center_x, g.center_y, 10.0f, g.center_x, g.center_y, 0.0f, 0, 1, 0);
    sk_camera3d_set_ortho_height(g.camera, g.zoom);
}

/* One cell of the sheet in the world. Ground tiles sit at z 0, props just in front
 * of them so they draw over the ground (by depth). */
static sk_handle_t add_sprite(const float cell[4], float x, float y, float z, float width, float height,
                              float pivot_y, int layer)
{
    const sk_handle_t sprite = sk_sprite3d_create(g.texture);
    sk_sprite3d_set_facing(sprite, SK_SPRITE3D_FACING_FREE);
    sk_sprite3d_set_source(sprite, cell[0], cell[1], cell[2], cell[3]);
    sk_sprite3d_set_extent(sprite, width, height);
    sk_sprite3d_set_pivot(sprite, 0.5f, pivot_y);
    sk_sprite3d_set_transform(sprite, x, y, z, 0, 0, 0, 1, 1, 1);
    /* pixel art has no soft edges: ground tiles are opaque, props cut out their
       transparent texels; neither needs sorting, so they draw in one batch each */
    sk_sprite3d_set_alpha_mode(sprite, layer == LAYER_GROUND ? SK_ALPHA_OPAQUE : SK_ALPHA_MASK, 0.5f);
    sk_scene_add(g.scene, sprite, layer);
    return sprite;
}

static void add_prop(const float cell[4], float x, float y, float width, float height, bool coin)
{
    if (g.prop_count >= MAX_PROPS) {
        return;
    }
    /* the pivot is the bottom edge, so a prop stands on its cell whatever its height */
    g.props[g.prop_count] = add_sprite(cell, x, y - 0.5f, 0.1f, width, height, 1.0f, LAYER_PROPS);
    g.is_coin[g.prop_count] = coin;
    if (coin) {
        sk_sprite3d_set_pick_alpha_test(g.props[g.prop_count], true, 0.5f);
    } else {
        sk_sprite3d_set_pickable(g.props[g.prop_count], false);
    }
    g.prop_count++;
}

/* A small hand-made map: water along the bottom, a sand shore, stone paths. */
static const float *tile_at(int x, int y)
{
    if (y < 2) return WATER;
    if (y < 3) return SAND;
    if (x == 8 || y == 9) return STONE;
    return GRASS;
}

static void on_tiles(const char *path, void *user)
{
    (void)user;
    g.texture = sk_texture_create(path);
    /* pixel art: keep the texels crisp when zoomed in */
    sk_texture_set_sampling(g.texture, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_NEAREST);

    for (int y = 0; y < WORLD_H; y++) {
        for (int x = 0; x < WORLD_W; x++) {
            /* a hair over one unit: neighbouring quads are blended separately, so
               edges that land exactly on a pixel boundary would let the background
               through as a hairline seam */
            g.ground[y * WORLD_W + x] = add_sprite(tile_at(x, y), (float)x + 0.5f, (float)y + 0.5f, 0.0f, 1.01f,
                                                   1.01f, 0.5f, LAYER_GROUND);
            sk_sprite3d_set_pickable(g.ground[y * WORLD_W + x], false);
        }
    }
    for (int i = 0; i < 7; i++) { /* trees and flags: 16x32 cells drawn 1x2 */
        add_prop(TREE, 2.5f + (float)i * 3.0f, 12.5f - (float)(i % 3), 1.0f, 2.0f, false);
    }
    add_prop(FLAG, 8.5f, 9.5f, 1.0f, 2.0f, false);
    for (int i = 0; i < 6; i++) {
        add_prop(ROCK, 4.5f + (float)i * 3.5f, 4.5f + (float)(i % 2) * 2.0f, 1.0f, 1.0f, false);
    }
    for (int i = 0; i < 8; i++) {
        add_prop(COIN, 3.5f + (float)i * 2.5f, 7.5f + (float)(i % 3), 0.8f, 0.8f, true);
    }
    g.loaded = true;
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_rgba(24, 28, 38, 255);
    g.shade = sk_color_rgba(18, 20, 28, 190);
    g.text = sk_color_rgba(235, 238, 245, 255);
    g.dim = sk_color_rgba(140, 146, 158, 255);
    g.highlight = sk_color_rgba(255, 230, 140, 255);

    g.center_x = WORLD_W * 0.5f;
    g.center_y = WORLD_H * 0.5f;
    g.zoom = 12.0f;
    g.camera = sk_camera3d_create(SK_CAMERA3D_ORTHOGRAPHIC);
    place_camera();

    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);
    sk_scene_set_interactive(g.scene, true);

    sk_asset_add_task(sk_asset_ensure_async(TILES_PATH, NULL, SK_ASSET_NONE), on_tiles, on_failed, NULL);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    const vec2_t screen = sk_window_get_screen_size();
    const float units_per_pixel = screen.y > 0.0f ? g.zoom / screen.y : 0.0f;
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();

    /* scroll: drag, or the arrow keys */
    if (mouse.buttons[0] == SK_BUTTON_DOWN) {
        g.center_x -= (float)mouse.dx * units_per_pixel;
        g.center_y += (float)mouse.dy * units_per_pixel; /* screen y is down, world y is up */
    }
    {
        const float speed = g.zoom * 0.6f * dt;
        if (kb.keys[SK_KEY_LEFT] >= SK_BUTTON_PRESSED && kb.keys[SK_KEY_LEFT] != SK_BUTTON_RELEASED) g.center_x -= speed;
        if (kb.keys[SK_KEY_RIGHT] >= SK_BUTTON_PRESSED && kb.keys[SK_KEY_RIGHT] != SK_BUTTON_RELEASED) g.center_x += speed;
        if (kb.keys[SK_KEY_DOWN] >= SK_BUTTON_PRESSED && kb.keys[SK_KEY_DOWN] != SK_BUTTON_RELEASED) g.center_y -= speed;
        if (kb.keys[SK_KEY_UP] >= SK_BUTTON_PRESSED && kb.keys[SK_KEY_UP] != SK_BUTTON_RELEASED) g.center_y += speed;
    }
    if (mouse.wheel != 0) { /* zoom: fewer world units visible = closer */
        g.zoom -= (float)mouse.wheel * 1.5f;
        g.zoom = g.zoom < 4.0f ? 4.0f : g.zoom > 32.0f ? 32.0f : g.zoom;
    }
    g.center_x = g.center_x < 0.0f ? 0.0f : g.center_x > (float)WORLD_W ? (float)WORLD_W : g.center_x;
    g.center_y = g.center_y < 0.0f ? 0.0f : g.center_y > (float)WORLD_H ? (float)WORLD_H : g.center_y;
    place_camera();

    /* coins: hover lights them up, a click collects them */
    for (int i = 0; i < g.prop_count; i++) {
        const sk_button_state_t hover = sk_scene_get_hover(g.scene, g.props[i]);
        if (!g.is_coin[i]) continue;
        sk_sprite3d_set_tint(g.props[i],
                             hover == SK_BUTTON_PRESSED || hover == SK_BUTTON_DOWN ? g.highlight : SK_COLOR_WHITE);
        if (sk_scene_is_clicked(g.scene, g.props[i])) {
            sk_sprite3d_set_visible(g.props[i], false);
            sk_sprite3d_set_pickable(g.props[i], false);
            g.collected++;
        }
    }

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_scene_draw(g.scene);
    sk_render_begin_mode_2d(); /* back to screen space for the HUD */
    sk_shape2d_draw_rectangle(0, 0, (int)screen.x, 88, g.shade);
    sk_text_draw("libsk 2d: an orthographic camera over sprite3d tiles", 20, 20, 20, g.text);
    snprintf(line, sizeof(line), "coins: %d of 8   zoom: %.1f units   center: %.1f, %.1f%s", g.collected, (double)g.zoom,
             (double)g.center_x, (double)g.center_y, g.loaded ? "" : "   (loading)");
    sk_text_draw(line, 20, 46, 15, g.dim);
    sk_text_draw("drag or arrows to scroll, wheel to zoom, click the coins", 20, 68, 15, g.dim);
    sk_render_end();
}

int main(void)
{
    /* no MSAA: tiles are blended quads that meet edge to edge, and multisampled
       edges let the background through as a hairline seam between them */
    sk_init_values(960, 600, "libsk 2d", 0);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
