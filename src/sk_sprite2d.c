#include "sk_sprite2d.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "sk_color.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite2d.h"
#include "internal/sk_sprite_batch.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"
#include "sk_texture.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

/* The sprite2d pool starts at SPRITES_INITIAL slots and doubles as needed, up to
 * SK_MAX_SPRITE2D (overridable at build time, -DSK_MAX_SPRITE2D=...). */
#ifndef SK_MAX_SPRITE2D
#define SK_MAX_SPRITE2D SK_HANDLE_POOL_MAX_SLOTS
#endif
#define SPRITES_INITIAL 256

typedef struct {
    sk_handle_t texture;
    float source_x, source_y, source_width, source_height; /* width/height <= 0: whole texture */
    float x, y;
    float rotation;
    float scale_x, scale_y;
    float width, height; /* <= 0: source size */
    float pivot_x, pivot_y;
    float slice_left, slice_top, slice_right, slice_bottom; /* nine-slice borders in source pixels; all 0: off */
    sk_color_t tint;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
    bool alpha_test;
    float alpha_threshold;
    sk_alpha_mode_t alpha_mode;
    float alpha_cutoff; /* SK_ALPHA_MASK */
} sk_sprite2d_t;

static sk_sprite2d_t *sk_sprites2d; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_sprite2d_pool;

static void draw_handle(sk_handle_t sprite);
static bool pick_handle(sk_handle_t sprite, float screen_x, float screen_y, sk_pick_result_t *out);

void sk_sprite2d_init(void)
{
    if (!sk_handle_pool_init(&sk_sprite2d_pool, SK_HANDLE_KIND_SPRITE2D, "sprite2d", (void **)&sk_sprites2d,
                             sizeof(sk_sprite2d_t), SPRITES_INITIAL, SK_MAX_SPRITE2D)) {
        log_error("sprite2d: out of memory");
    }
    sk_scene_register_2d(SK_HANDLE_KIND_SPRITE2D, draw_handle, pick_handle);
    sk_scene_register_enabled(SK_HANDLE_KIND_SPRITE2D, sk_sprite2d_is_enabled);
}

void sk_sprite2d_deinit(void)
{
    for (uint16_t i = 1; i < sk_sprite2d_pool.capacity; i++) {
        if (sk_sprite2d_pool.occupied[i] && sk_sprites2d[i].texture != 0) {
            sk_texture_release(sk_sprites2d[i].texture);
        }
    }
    sk_handle_pool_destroy(&sk_sprite2d_pool);
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

/* Screen position of a point given in unit coordinates across the sprite. */
static void point_at(const sk_sprite2d_placement_t *p, float u, float v, float *out_x, float *out_y)
{
    const float c = cosf(p->rotation), s = sinf(p->rotation);
    const float lx = (u - p->pivot_x) * p->width * p->scale_x;
    const float ly = (v - p->pivot_y) * p->height * p->scale_y;
    *out_x = p->x + lx * c - ly * s;
    *out_y = p->y + lx * s + ly * c;
}

void sk_sprite2d_corners(const sk_sprite2d_placement_t *p, float out[8])
{
    static const float unit[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; i++) {
        point_at(p, unit[i][0], unit[i][1], &out[i * 2], &out[i * 2 + 1]);
    }
}

bool sk_sprite2d_nine_slice_axis(float border_low, float border_high, float dest_size, float source_size,
                                 float out_dest[2], float out_source[2])
{
    float low = border_low > 0.0f ? border_low : 0.0f;
    float high = border_high > 0.0f ? border_high : 0.0f;
    float dest_low, dest_high;

    if (dest_size <= 0.0f || source_size <= 0.0f || low + high <= 0.0f) {
        return false;
    }
    if (low + high > source_size) { /* borders wider than the region: share it */
        const float k = source_size / (low + high);
        low *= k;
        high *= k;
    }
    dest_low = low;
    dest_high = high;
    if (dest_low + dest_high > dest_size) { /* too big to draw at 1:1: shrink them to fit */
        const float k = dest_size / (dest_low + dest_high);
        dest_low *= k;
        dest_high *= k;
    }
    out_dest[0] = dest_low / dest_size;
    out_dest[1] = 1.0f - dest_high / dest_size;
    out_source[0] = low / source_size;
    out_source[1] = 1.0f - high / source_size;
    return true;
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

static bool is_nine_slice(const sk_sprite2d_t *sprite_ptr)
{
    return sprite_ptr->slice_left > 0.0f || sprite_ptr->slice_top > 0.0f || sprite_ptr->slice_right > 0.0f ||
           sprite_ptr->slice_bottom > 0.0f;
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

/* Textured quad in 2D: a sprite's goes to the instanced sprite path with its alpha
 * mode (sprite_ptr), an immediate one (sk_texture_draw*, sprite_ptr NULL) through
 * sokol_gl's 2D projection. corners: top-left, top-right, bottom-right, bottom-left. */
static void draw_quad(const sk_sprite2d_t *sprite_ptr, sg_view view, sg_sampler smp, const float corners[8], float u0,
                      float v0, float u1, float v1, bool flip_v, sk_color_t tint)
{
    const sk_colorf_t c = sk_color_unpack(tint);
    if (flip_v) { /* render target stored bottom-up (see sk_texture_is_flipped) */
        v0 = 1.0f - v0;
        v1 = 1.0f - v1;
    }
    if (sprite_ptr != NULL) {
        /* the top-left corner, and the top and left edges as the quad's axes */
        const sk_sprite_quad_t quad = {
            .position = {corners[0], corners[1], 0.0f},
            .facing = 2.0f,
            .size = {1.0f, 1.0f},
            .uv = {u0, v0, u1, v1},
            .right = {corners[2] - corners[0], corners[3] - corners[1], 0.0f},
            .up = {corners[0] - corners[6], corners[1] - corners[7], 0.0f},
            .alpha = sprite_ptr->alpha_mode == SK_ALPHA_MASK     ? fmaxf(sprite_ptr->alpha_cutoff, 1e-6f)
                     : sprite_ptr->alpha_mode == SK_ALPHA_OPAQUE ? -1.0f
                                                                 : 0.0f,
            .color = {(uint8_t)sk_color_get_red(tint), (uint8_t)sk_color_get_green(tint),
                      (uint8_t)sk_color_get_blue(tint), (uint8_t)sk_color_get_alpha(tint)},
        };
        sk_sprite_batch_add_2d(&quad, view.id, smp.id, sprite_ptr->alpha_mode);
        return;
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

/* Nine-slice: the corners keep their size, the edges stretch along one axis and the
 * middle along both. An axis without borders stays one span, so a sprite sliced on
 * one axis draws three patches, not nine. False when nothing is sliced. */
static bool draw_nine_slice(const sk_sprite2d_t *sprite_ptr, sg_view view, sg_sampler smp, bool flip_v,
                            const float source[4], int tw, int th, const sk_sprite2d_placement_t *p,
                            const float slice[4], sk_color_t tint)
{
    float du[4] = {0.0f, 1.0f, 0.0f, 0.0f}, su[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float dv[4] = {0.0f, 1.0f, 0.0f, 0.0f}, sv[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    int nu = 2, nv = 2;
    float d[2], t[2];

    /* slice: left, top, right, bottom borders in source pixels */
    if (sk_sprite2d_nine_slice_axis(slice[0], slice[2], p->width, source[2], d, t)) {
        du[1] = d[0], du[2] = d[1], du[3] = 1.0f;
        su[1] = t[0], su[2] = t[1], su[3] = 1.0f;
        nu = 4;
    }
    if (sk_sprite2d_nine_slice_axis(slice[1], slice[3], p->height, source[3], d, t)) {
        dv[1] = d[0], dv[2] = d[1], dv[3] = 1.0f;
        sv[1] = t[0], sv[2] = t[1], sv[3] = 1.0f;
        nv = 4;
    }
    if (nu == 2 && nv == 2) {
        return false;
    }
    for (int j = 0; j + 1 < nv; j++) {
        for (int i = 0; i + 1 < nu; i++) {
            float corners[8];
            if (du[i + 1] <= du[i] || dv[j + 1] <= dv[j]) {
                continue; /* a border shrunk away */
            }
            point_at(p, du[i], dv[j], &corners[0], &corners[1]);
            point_at(p, du[i + 1], dv[j], &corners[2], &corners[3]);
            point_at(p, du[i + 1], dv[j + 1], &corners[4], &corners[5]);
            point_at(p, du[i], dv[j + 1], &corners[6], &corners[7]);
            draw_quad(sprite_ptr, view, smp, corners, (source[0] + su[i] * source[2]) / (float)tw,
                      (source[1] + sv[j] * source[3]) / (float)th,
                      (source[0] + su[i + 1] * source[2]) / (float)tw,
                      (source[1] + sv[j + 1] * source[3]) / (float)th, flip_v, tint);
        }
    }
    return true;
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
    const float slice[4] = {sprite_ptr->slice_left, sprite_ptr->slice_top, sprite_ptr->slice_right,
                            sprite_ptr->slice_bottom};
    if (draw_nine_slice(sprite_ptr, view, smp, sk_texture_is_flipped(sprite_ptr->texture), source, tw, th, &placement,
                        slice, sprite_ptr->tint)) {
        return;
    }
    sk_sprite2d_corners(&placement, corners);
    draw_quad(sprite_ptr, view, smp, corners, source[0] / (float)tw, source[1] / (float)th,
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
    if (sprite_ptr->alpha_test && !is_nine_slice(sprite_ptr) &&
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
        log_error("sprite2d: pool full (%u)", (unsigned)sk_sprite2d_pool.max - 1u);
        return 0;
    }
    sk_handle_pool_resolve(&sk_sprite2d_pool, handle, &index);
    sk_sprites2d[index] = (sk_sprite2d_t){
        .texture = texture,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
        .tint = SK_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
        .alpha_threshold = 0.5f,
        .alpha_mode = SK_ALPHA_BLEND,
        .alpha_cutoff = 0.5f,
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
    sk_scene_forget(sprite);
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
bool sk_sprite2d_set_nine_slice(sk_handle_t sprite, float left, float top, float right, float bottom)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->slice_left = left > 0.0f ? left : 0.0f;
    sprite_ptr->slice_top = top > 0.0f ? top : 0.0f;
    sprite_ptr->slice_right = right > 0.0f ? right : 0.0f;
    sprite_ptr->slice_bottom = bottom > 0.0f ? bottom : 0.0f;
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
bool sk_sprite2d_set_tint(sk_handle_t sprite, sk_color_t color)
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
bool sk_sprite2d_set_enabled(sk_handle_t sprite, bool enabled)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_sprite2d_is_enabled(sk_handle_t sprite)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->enabled;
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
bool sk_sprite2d_set_alpha_mode(sk_handle_t sprite, sk_alpha_mode_t mode, float cutoff)
{
    sk_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL || mode < SK_ALPHA_OPAQUE || mode > SK_ALPHA_ADD) {
        return false;
    }
    sprite_ptr->alpha_mode = mode;
    sprite_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff > 1.0f ? 1.0f : cutoff;
    return true;
}

SK_KEEP
sk_alpha_mode_t sk_sprite2d_get_alpha_mode(sk_handle_t sprite)
{
    const sk_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL ? sprite_ptr->alpha_mode : SK_ALPHA_BLEND;
}

SK_KEEP
void sk_sprite2d_draw(sk_handle_t sprite)
{
    draw_handle(sprite);
}

/* The texture's region in pixels: the whole texture when width or height <= 0. */
static void texture_source(float source_x, float source_y, float source_width, float source_height, int tw, int th,
                           float out[4])
{
    if (source_width > 0.0f && source_height > 0.0f) {
        out[0] = source_x, out[1] = source_y, out[2] = source_width, out[3] = source_height;
    } else {
        out[0] = 0.0f, out[1] = 0.0f, out[2] = (float)tw, out[3] = (float)th;
    }
}

SK_KEEP
void sk_texture_draw(sk_handle_t texture, float x, float y, float width, float height, sk_color_t tint)
{
    sk_texture_draw_ex(texture, 0.0f, 0.0f, 0.0f, 0.0f, x, y, width, height, tint);
}

SK_KEEP
void sk_texture_draw_ex(sk_handle_t texture, float source_x, float source_y, float source_width, float source_height,
                        float x, float y, float width, float height, sk_color_t tint)
{
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;
    float source[4], corners[8];

    if (texture == 0 || !sk_texture_get_binding(texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    texture_source(source_x, source_y, source_width, source_height, tw, th, source);
    if (width <= 0.0f || height <= 0.0f) { /* the region's own size */
        width = source[2];
        height = source[3];
    }
    corners[0] = x;         corners[1] = y;
    corners[2] = x + width; corners[3] = y;
    corners[4] = x + width; corners[5] = y + height;
    corners[6] = x;         corners[7] = y + height;
    draw_quad(NULL, view, smp, corners, source[0] / (float)tw, source[1] / (float)th,
              (source[0] + source[2]) / (float)tw, (source[1] + source[3]) / (float)th, sk_texture_is_flipped(texture),
              tint);
}

SK_KEEP
void sk_texture_draw_nine_slice(sk_handle_t texture, float source_x, float source_y, float source_width,
                                float source_height, float left, float top, float right, float bottom, float x,
                                float y, float width, float height, sk_color_t tint)
{
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;
    float source[4];
    const float slice[4] = {fmaxf(0.0f, left), fmaxf(0.0f, top), fmaxf(0.0f, right), fmaxf(0.0f, bottom)};
    const sk_sprite2d_placement_t placement = {
        .x = x, .y = y, .width = width, .height = height, .scale_x = 1.0f, .scale_y = 1.0f,
    }; /* pivot (0, 0): (x, y) is the top-left corner */

    if (texture == 0 || width <= 0.0f || height <= 0.0f ||
        !sk_texture_get_binding(texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    texture_source(source_x, source_y, source_width, source_height, tw, th, source);
    if (!draw_nine_slice(NULL, view, smp, sk_texture_is_flipped(texture), source, tw, th, &placement, slice, tint)) {
        sk_texture_draw_ex(texture, source[0], source[1], source[2], source[3], x, y, width, height, tint);
    }
}
