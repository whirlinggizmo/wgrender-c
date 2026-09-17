#include "sk_sprite2d.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite2d.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"
#include "sk_texture.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define MAX_SPRITES 4096

typedef struct {
    sk_handle_t texture;
    float source_x, source_y, source_width, source_height; /* width/height <= 0: whole texture */
    float x, y;
    float rotation;
    float scale_x, scale_y;
    float width, height; /* <= 0: source size */
    float pivot_x, pivot_y;
    sk_handle_t tint;
    bool visible;
    bool pickable;
    bool alpha_test;
    float alpha_threshold;
} sk_sprite2d_t;

static sk_sprite2d_t sk_sprites2d[MAX_SPRITES];
static sk_handle_pool_t sk_sprite2d_pool;
static uint16_t sk_sprite2d_free_indices[MAX_SPRITES];
static uint16_t sk_sprite2d_generations[MAX_SPRITES];
static unsigned char sk_sprite2d_occupied[MAX_SPRITES];

static void draw_handle(sk_handle_t sprite);
static bool pick_handle(sk_handle_t sprite, float screen_x, float screen_y, sk_pick_result_t *out);

void sk_sprite2d_init(void)
{
    memset(sk_sprites2d, 0, sizeof(sk_sprites2d));
    sk_handle_pool_init(&sk_sprite2d_pool, SK_HANDLE_KIND_SPRITE2D, MAX_SPRITES, sk_sprite2d_free_indices,
                        MAX_SPRITES, sk_sprite2d_generations, sk_sprite2d_occupied);
    sk_scene_register_2d(SK_HANDLE_KIND_SPRITE2D, draw_handle, pick_handle);
}

void sk_sprite2d_deinit(void)
{
    for (uint16_t i = 1; i < MAX_SPRITES; i++) {
        if (sk_sprite2d_occupied[i] && sk_sprites2d[i].texture != 0) {
            sk_texture_release(sk_sprites2d[i].texture);
        }
    }
    sk_handle_pool_reset(&sk_sprite2d_pool);
}

static sk_sprite2d_t *resolve(sk_handle_t sprite)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_sprite2d_pool, sprite, &index)) {
        if (sprite != 0) {
            log_warn("Invalid sprite2d handle (%u)", (unsigned int)sprite);
        }
        return NULL;
    }
    return &sk_sprites2d[index];
}

/* ------------------------------------------------------------ geometry ---- */

void sk_sprite2d_corners(const sk_sprite2d_placement_t *p, float out[8])
{
    static const float unit[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const float c = cosf(p->rotation), s = sinf(p->rotation);
    for (int i = 0; i < 4; i++) {
        const float lx = (unit[i][0] - p->pivot_x) * p->width * p->scale_x;
        const float ly = (unit[i][1] - p->pivot_y) * p->height * p->scale_y;
        out[i * 2] = p->x + lx * c - ly * s;
        out[i * 2 + 1] = p->y + lx * s + ly * c;
    }
}

bool sk_sprite2d_screen_to_unit(const sk_sprite2d_placement_t *p, float screen_x, float screen_y,
                                float *u, float *v)
{
    const float span_x = p->width * p->scale_x, span_y = p->height * p->scale_y;
    const float c = cosf(p->rotation), s = sinf(p->rotation);
    const float dx = screen_x - p->x, dy = screen_y - p->y;
    float ux, uy;

    if (span_x == 0.0f || span_y == 0.0f) {
        return false;
    }
    /* undo rotation, then scale and pivot */
    ux = (dx * c + dy * s) / span_x + p->pivot_x;
    uy = (-dx * s + dy * c) / span_y + p->pivot_y;
    if (ux < 0.0f || ux > 1.0f || uy < 0.0f || uy > 1.0f) {
        return false;
    }
    *u = ux;
    *v = uy;
    return true;
}

/* Resolve a sprite's texture binding, source region (texture pixels) and
 * placement. False when there's nothing to draw yet. */
static bool resolve_placement(const sk_sprite2d_t *sprite_ptr, sg_view *view, sg_sampler *smp,
                              float source[4], int *texture_width, int *texture_height,
                              sk_sprite2d_placement_t *placement)
{
    int tw = 0, th = 0;
    if (sprite_ptr->texture == 0 || !sk_texture_get_binding(sprite_ptr->texture, view, smp, &tw, &th) ||
        tw <= 0 || th <= 0) {
        return false;
    }
    if (sprite_ptr->source_width > 0.0f && sprite_ptr->source_height > 0.0f) {
        source[0] = sprite_ptr->source_x;
        source[1] = sprite_ptr->source_y;
        source[2] = sprite_ptr->source_width;
        source[3] = sprite_ptr->source_height;
    } else {
        source[0] = 0.0f;
        source[1] = 0.0f;
        source[2] = (float)tw;
        source[3] = (float)th;
    }
    *texture_width = tw;
    *texture_height = th;
    *placement = (sk_sprite2d_placement_t){
        .x = sprite_ptr->x,
        .y = sprite_ptr->y,
        .width = sprite_ptr->width > 0.0f && sprite_ptr->height > 0.0f ? sprite_ptr->width : source[2],
        .height = sprite_ptr->width > 0.0f && sprite_ptr->height > 0.0f ? sprite_ptr->height : source[3],
        .scale_x = sprite_ptr->scale_x,
        .scale_y = sprite_ptr->scale_y,
        .pivot_x = sprite_ptr->pivot_x,
        .pivot_y = sprite_ptr->pivot_y,
        .rotation = sprite_ptr->rotation,
    };
    return true;
}

/* Textured quad in the current (2D) sokol_gl projection. */
static void draw_quad(sg_view view, sg_sampler smp, const float corners[8], float u0, float v0, float u1,
                      float v1, bool flip_v, sk_handle_t tint)
{
    const color_t c = sk_color_get(tint);
    if (flip_v) { /* render target stored bottom-up (see sk_texture_is_flipped) */
        v0 = 1.0f - v0;
        v1 = 1.0f - v1;
    }
    sgl_enable_texture();
    sgl_texture(view, smp);
    sgl_begin_quads();
    sgl_c4f(c.r, c.g, c.b, c.a);
    sgl_v2f_t2f(corners[0], corners[1], u0, v0);
    sgl_v2f_t2f(corners[2], corners[3], u1, v0);
    sgl_v2f_t2f(corners[4], corners[5], u1, v1);
    sgl_v2f_t2f(corners[6], corners[7], u0, v1);
    sgl_end();
    sgl_disable_texture();
}

static void draw_handle(sk_handle_t sprite)
{
    const sk_sprite2d_t *sprite_ptr = resolve(sprite);
    sk_sprite2d_placement_t placement;
    sg_view view;
    sg_sampler smp;
    float source[4], corners[8];
    int tw, th;

    if (sprite_ptr == NULL || !sprite_ptr->visible ||
        !resolve_placement(sprite_ptr, &view, &smp, source, &tw, &th, &placement)) {
        return;
    }
    sk_sprite2d_corners(&placement, corners);
    draw_quad(view, smp, corners, source[0] / (float)tw, source[1] / (float)th,
              (source[0] + source[2]) / (float)tw, (source[1] + source[3]) / (float)th,
              sk_texture_is_flipped(sprite_ptr->texture), sprite_ptr->tint);
}

static bool pick_handle(sk_handle_t sprite, float screen_x, float screen_y, sk_pick_result_t *out)
{
    const sk_sprite2d_t *sprite_ptr = resolve(sprite);
    sk_sprite2d_placement_t placement;
    sg_view view;
    sg_sampler smp;
    float source[4], u, v, alpha;
    int tw, th;

    if (sprite_ptr == NULL || !sprite_ptr->visible || !sprite_ptr->pickable ||
        !resolve_placement(sprite_ptr, &view, &smp, source, &tw, &th, &placement) ||
        !sk_sprite2d_screen_to_unit(&placement, screen_x, screen_y, &u, &v)) {
        return false;
    }
    if (sprite_ptr->alpha_test &&
        sk_texture_sample_alpha(sprite_ptr->texture, (source[0] + u * source[2]) / (float)tw,
                                (source[1] + v * source[3]) / (float)th, &alpha) &&
        alpha < sprite_ptr->alpha_threshold) {
        return false;
    }
    *out = (sk_pick_result_t){
        .hit = true,
        .handle = sprite,
        .distance = 0.0f, /* 2D hits have no depth */
        .point_world = {screen_x, screen_y, 0.0f},
        .point_local = {u, v, 0.0f},
        .normal_world = {0.0f, 0.0f, 1.0f},
        .normal_local = {0.0f, 0.0f, 1.0f},
    };
    return true;
}

/* ------------------------------------------------------------ public API ---- */

SK_KEEP
sk_handle_t sk_sprite2d_create(sk_handle_t texture)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_sprite2d_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SPRITES reached (%d)", MAX_SPRITES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_sprite2d_pool, handle, &index);
    sk_sprites2d[index] = (sk_sprite2d_t){
        .texture = texture,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
        .visible = true,
        .pickable = true,
        .alpha_threshold = 0.5f,
    };
    if (texture != 0) {
        sk_texture_retain(texture);
    }
    return handle;
}

SK_KEEP
void sk_sprite2d_destroy(sk_handle_t sprite)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return;
    }
    if (sprite_ptr->texture != 0) {
        sk_texture_release(sprite_ptr->texture);
    }
    *sprite_ptr = (sk_sprite2d_t){0};
    sk_handle_pool_free(&sk_sprite2d_pool, sprite);
}

SK_KEEP
bool sk_sprite2d_set_texture(sk_handle_t sprite, sk_handle_t texture)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (sprite_ptr->texture == texture) {
        return true;
    }
    if (texture != 0) {
        sk_texture_retain(texture);
    }
    if (sprite_ptr->texture != 0) {
        sk_texture_release(sprite_ptr->texture);
    }
    sprite_ptr->texture = texture;
    if (sprite_ptr->alpha_test && texture != 0) {
        sk_texture_ensure_alpha_mask(texture);
    }
    return true;
}

SK_KEEP
bool sk_sprite2d_set_source(sk_handle_t sprite, float x, float y, float width, float height)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->source_x = x;
    sprite_ptr->source_y = y;
    sprite_ptr->source_width = width > 0.0f && height > 0.0f ? width : 0.0f;
    sprite_ptr->source_height = width > 0.0f && height > 0.0f ? height : 0.0f;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_position(sk_handle_t sprite, float x, float y)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->x = x;
    sprite_ptr->y = y;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_rotation(sk_handle_t sprite, float angle)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->rotation = angle;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_scale(sk_handle_t sprite, float x, float y)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->scale_x = x;
    sprite_ptr->scale_y = y;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_size(sk_handle_t sprite, float width, float height)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->width = width > 0.0f && height > 0.0f ? width : 0.0f;
    sprite_ptr->height = width > 0.0f && height > 0.0f ? height : 0.0f;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_pivot(sk_handle_t sprite, float x, float y)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->pivot_x = x;
    sprite_ptr->pivot_y = y;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_tint(sk_handle_t sprite, sk_handle_t color)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->tint = color;
    return true;
}

SK_KEEP
bool sk_sprite2d_set_visible(sk_handle_t sprite, bool visible)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_sprite2d_is_visible(sk_handle_t sprite)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->visible;
}

SK_KEEP
bool sk_sprite2d_set_pickable(sk_handle_t sprite, bool pickable)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_sprite2d_is_pickable(sk_handle_t sprite)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->pickable;
}

SK_KEEP
bool sk_sprite2d_set_pick_alpha_test(sk_handle_t sprite, bool enable, float threshold)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->alpha_test = enable;
    sprite_ptr->alpha_threshold = threshold;
    if (enable && sprite_ptr->texture != 0) {
        sk_texture_ensure_alpha_mask(sprite_ptr->texture);
    }
    return true;
}

SK_KEEP
void sk_sprite2d_draw(sk_handle_t sprite)
{
    draw_handle(sprite);
}

SK_KEEP
void sk_texture_draw(sk_handle_t texture, float x, float y, float width, float height, sk_handle_t tint)
{
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;
    float corners[8];

    if (texture == 0 || !sk_texture_get_binding(texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    if (width <= 0.0f || height <= 0.0f) {
        width = (float)tw;
        height = (float)th;
    }
    corners[0] = x;         corners[1] = y;
    corners[2] = x + width; corners[3] = y;
    corners[4] = x + width; corners[5] = y + height;
    corners[6] = x;         corners[7] = y + height;
    draw_quad(view, smp, corners, 0.0f, 0.0f, 1.0f, 1.0f, sk_texture_is_flipped(texture), tint);
}
