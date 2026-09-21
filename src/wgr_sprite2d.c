#include "wgr_sprite2d.h"

#include <math.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite2d_internal.h"
#include "internal/wgr_sprite_batch_internal.h"
#include "internal/wgr_texture_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_material_internal.h"
#include "wgr_logger.h"
#include "wgr_texture.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

/* The sprite2d pool starts at SPRITES_INITIAL slots and doubles as needed, up to
 * WGR_MAX_SPRITE2D (overridable at build time, -DWGR_MAX_SPRITE2D=...). */
#ifndef WGR_MAX_SPRITE2D
#define WGR_MAX_SPRITE2D WGRI_HANDLE_POOL_MAX_SLOTS
#endif
#define SPRITES_INITIAL 256

typedef struct {
    wgr_handle_t texture;
    float source_x, source_y, source_width, source_height; /* width/height <= 0: whole texture */
    float x, y;
    float rotation;
    float scale_x, scale_y;
    float width, height; /* <= 0: source size */
    float pivot_x, pivot_y;
    float slice_left, slice_top, slice_right, slice_bottom; /* nine-slice borders in source pixels; all 0: off */
    wgr_color_t tint;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
    bool alpha_test;
    float alpha_threshold;
    wgr_alpha_mode_t alpha_mode;
    float alpha_cutoff; /* WGR_ALPHA_MASK */
    wgr_handle_t material; /* a custom material (referenced), or 0 */
} wgr_sprite2d_t;

/* Swap `*slot` for `material` (a custom material, or 0), keeping one reference. */
static bool assign_material(wgr_handle_t *slot, wgr_handle_t material, const char *who)
{
    const wgri_material_t *material_ptr = material != 0 ? wgri_material_get(material) : NULL;
    if (material != 0 && (material_ptr == NULL || material_ptr->shader == 0)) {
        /* built-in materials light a surface, and 2D has no lights (wgr_sprite3d) */
        log_warn("%s: 2D sprites take custom materials (wgr_material_create_custom) or 0", who);
        return false;
    }
    if (wgri_material_is_screen(material)) {
        log_warn("%s: that material's shader is a screen effect (wgr_render_add_effect), not a surface shader", who);
        return false;
    }
    if (*slot != material) {
        wgri_material_retain(material); /* no-op for 0 */
        wgr_material_release(*slot);
        *slot = material;
    }
    return true;
}

static wgr_sprite2d_t *wgr_sprites2d; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_sprite2d_pool;

static void draw_handle(wgr_handle_t sprite);
static bool pick_handle(wgr_handle_t sprite, float screen_x, float screen_y, wgr_pick_result_t *out);

void wgri_sprite2d_init(void)
{
    wgri_sprite_batch_init(); /* shared with sprite3d: counted */
    if (!wgri_handle_pool_init(&wgr_sprite2d_pool, WGR_HANDLE_KIND_SPRITE2D, "sprite2d", (void **)&wgr_sprites2d,
                             sizeof(wgr_sprite2d_t), SPRITES_INITIAL, WGR_MAX_SPRITE2D)) {
        log_error("sprite2d: out of memory");
    }
    wgri_scene_register_2d(WGR_HANDLE_KIND_SPRITE2D, draw_handle, pick_handle);
    wgri_scene_register_enabled(WGR_HANDLE_KIND_SPRITE2D, wgr_sprite2d_is_enabled);
}

void wgri_sprite2d_deinit(void)
{
    for (uint16_t i = 1; i < wgr_sprite2d_pool.capacity; i++) {
        if (wgr_sprite2d_pool.occupied[i] && wgr_sprites2d[i].texture != 0) {
            wgr_texture_release(wgr_sprites2d[i].texture);
        }
    }
    wgri_handle_pool_destroy(&wgr_sprite2d_pool);
    wgri_sprite_batch_deinit();
}

static wgr_sprite2d_t *resolve(wgr_handle_t sprite)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_sprite2d_pool, sprite, &index)) {
        if (sprite != 0) {
            log_warn("Invalid sprite2d handle (%u)", (unsigned int)sprite);
        }
        return NULL;
    }
    return &wgr_sprites2d[index];
}

/* ------------------------------------------------------------ geometry ---- */

/* Screen position of a point given in unit coordinates across the sprite. */
static void point_at(const wgri_sprite2d_placement_t *p, float u, float v, float *out_x, float *out_y)
{
    const float c = cosf(p->rotation), s = sinf(p->rotation);
    const float lx = (u - p->pivot_x) * p->width * p->scale_x;
    const float ly = (v - p->pivot_y) * p->height * p->scale_y;
    *out_x = p->x + lx * c - ly * s;
    *out_y = p->y + lx * s + ly * c;
}

void wgri_sprite2d_corners(const wgri_sprite2d_placement_t *p, float out[8])
{
    static const float unit[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; i++) {
        point_at(p, unit[i][0], unit[i][1], &out[i * 2], &out[i * 2 + 1]);
    }
}

bool wgri_sprite2d_nine_slice_axis(float border_low, float border_high, float dest_size, float source_size,
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

bool wgri_sprite2d_screen_to_unit(const wgri_sprite2d_placement_t *p, float screen_x, float screen_y,
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

static bool is_nine_slice(const wgr_sprite2d_t *sprite_ptr)
{
    return sprite_ptr->slice_left > 0.0f || sprite_ptr->slice_top > 0.0f || sprite_ptr->slice_right > 0.0f ||
           sprite_ptr->slice_bottom > 0.0f;
}

/* Resolve a sprite's texture binding, source region (texture pixels) and
 * placement. False when there's nothing to draw yet. */
static bool resolve_placement(const wgr_sprite2d_t *sprite_ptr, sg_view *view, sg_sampler *smp,
                              float source[4], int *texture_width, int *texture_height,
                              wgri_sprite2d_placement_t *placement)
{
    int tw = 0, th = 0;
    if (sprite_ptr->texture == 0 || !wgri_texture_get_binding(sprite_ptr->texture, view, smp, &tw, &th) ||
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
    *placement = (wgri_sprite2d_placement_t){
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
 * mode (sprite_ptr), an immediate one (wgr_texture_draw*, sprite_ptr NULL) through
 * sokol_gl's 2D projection. corners: top-left, top-right, bottom-right, bottom-left. */
static void draw_quad(const wgr_sprite2d_t *sprite_ptr, sg_view view, sg_sampler smp, const float corners[8], float u0,
                      float v0, float u1, float v1, bool flip_v, wgr_color_t tint)
{
    const wgri_colorf_t c = wgri_color_unpack(tint);
    if (flip_v) { /* render target stored bottom-up (see wgri_texture_is_flipped) */
        v0 = 1.0f - v0;
        v1 = 1.0f - v1;
    }
    if (sprite_ptr != NULL) {
        /* the top-left corner, and the top and left edges as the quad's axes */
        const wgri_sprite_quad_t quad = {
            .position = {corners[0], corners[1], 0.0f},
            .facing = 2.0f,
            .size = {1.0f, 1.0f},
            .uv = {u0, v0, u1, v1},
            .right = {corners[2] - corners[0], corners[3] - corners[1], 0.0f},
            .up = {corners[0] - corners[6], corners[1] - corners[7], 0.0f},
            .alpha = sprite_ptr->alpha_mode == WGR_ALPHA_MASK     ? fmaxf(sprite_ptr->alpha_cutoff, 1e-6f)
                     : sprite_ptr->alpha_mode == WGR_ALPHA_OPAQUE ? -1.0f
                                                                 : 0.0f,
            .color = {(uint8_t)wgr_color_get_red(tint), (uint8_t)wgr_color_get_green(tint),
                      (uint8_t)wgr_color_get_blue(tint), (uint8_t)wgr_color_get_alpha(tint)},
        };
        wgri_sprite_batch_add_2d(&quad, view.id, smp.id, sprite_ptr->alpha_mode, sprite_ptr->material);
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
static bool draw_nine_slice(const wgr_sprite2d_t *sprite_ptr, sg_view view, sg_sampler smp, bool flip_v,
                            const float source[4], int tw, int th, const wgri_sprite2d_placement_t *p,
                            const float slice[4], wgr_color_t tint)
{
    float du[4] = {0.0f, 1.0f, 0.0f, 0.0f}, su[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    float dv[4] = {0.0f, 1.0f, 0.0f, 0.0f}, sv[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    int nu = 2, nv = 2;
    float d[2], t[2];

    /* slice: left, top, right, bottom borders in source pixels */
    if (wgri_sprite2d_nine_slice_axis(slice[0], slice[2], p->width, source[2], d, t)) {
        du[1] = d[0], du[2] = d[1], du[3] = 1.0f;
        su[1] = t[0], su[2] = t[1], su[3] = 1.0f;
        nu = 4;
    }
    if (wgri_sprite2d_nine_slice_axis(slice[1], slice[3], p->height, source[3], d, t)) {
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

static void draw_handle(wgr_handle_t sprite)
{
    const wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    wgri_sprite2d_placement_t placement;
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
    if (draw_nine_slice(sprite_ptr, view, smp, wgri_texture_is_flipped(sprite_ptr->texture), source, tw, th, &placement,
                        slice, sprite_ptr->tint)) {
        return;
    }
    wgri_sprite2d_corners(&placement, corners);
    draw_quad(sprite_ptr, view, smp, corners, source[0] / (float)tw, source[1] / (float)th,
              (source[0] + source[2]) / (float)tw, (source[1] + source[3]) / (float)th,
              wgri_texture_is_flipped(sprite_ptr->texture), sprite_ptr->tint);
}

static bool pick_handle(wgr_handle_t sprite, float screen_x, float screen_y, wgr_pick_result_t *out)
{
    const wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    wgri_sprite2d_placement_t placement;
    sg_view view;
    sg_sampler smp;
    float source[4], u, v, alpha;
    int tw, th;

    if (sprite_ptr == NULL || !sprite_ptr->visible || !sprite_ptr->pickable ||
        !resolve_placement(sprite_ptr, &view, &smp, source, &tw, &th, &placement) ||
        !wgri_sprite2d_screen_to_unit(&placement, screen_x, screen_y, &u, &v)) {
        return false;
    }
    if (sprite_ptr->alpha_test && !is_nine_slice(sprite_ptr) &&
        wgri_texture_sample_alpha(sprite_ptr->texture, (source[0] + u * source[2]) / (float)tw,
                                (source[1] + v * source[3]) / (float)th, &alpha) &&
        alpha < sprite_ptr->alpha_threshold) {
        return false;
    }
    *out = (wgr_pick_result_t){
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

WGRI_KEEP
wgr_handle_t wgr_sprite2d_create(wgr_handle_t texture)
{
    wgr_handle_t handle = wgri_handle_pool_alloc(&wgr_sprite2d_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("sprite2d: pool full (%u)", (unsigned)wgr_sprite2d_pool.max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_sprite2d_pool, handle, &index);
    wgr_sprites2d[index] = (wgr_sprite2d_t){
        .texture = texture,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .pivot_x = 0.5f,
        .pivot_y = 0.5f,
        .tint = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
        .alpha_threshold = 0.5f,
        .alpha_mode = WGR_ALPHA_BLEND,
        .alpha_cutoff = 0.5f,
    };
    if (texture != 0) {
        wgri_texture_retain(texture);
    }
    return handle;
}

WGRI_KEEP
void wgr_sprite2d_destroy(wgr_handle_t sprite)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return;
    }
    wgri_scene_forget(sprite);
    if (sprite_ptr->texture != 0) {
        wgr_texture_release(sprite_ptr->texture);
    }
    wgr_material_release(sprite_ptr->material); /* no-op for 0 */
    *sprite_ptr = (wgr_sprite2d_t){0};
    wgri_handle_pool_free(&wgr_sprite2d_pool, sprite);
}

WGRI_KEEP
bool wgr_sprite2d_set_texture(wgr_handle_t sprite, wgr_handle_t texture)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (sprite_ptr->texture == texture) {
        return true;
    }
    if (texture != 0) {
        wgri_texture_retain(texture);
    }
    if (sprite_ptr->texture != 0) {
        wgr_texture_release(sprite_ptr->texture);
    }
    sprite_ptr->texture = texture;
    if (sprite_ptr->alpha_test && texture != 0) {
        wgri_texture_ensure_alpha_mask(texture);
    }
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_source(wgr_handle_t sprite, float x, float y, float width, float height)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->source_x = x;
    sprite_ptr->source_y = y;
    sprite_ptr->source_width = width > 0.0f && height > 0.0f ? width : 0.0f;
    sprite_ptr->source_height = width > 0.0f && height > 0.0f ? height : 0.0f;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_position(wgr_handle_t sprite, float x, float y)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->x = x;
    sprite_ptr->y = y;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_rotation(wgr_handle_t sprite, float angle)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->rotation = angle;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_scale(wgr_handle_t sprite, float x, float y)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->scale_x = x;
    sprite_ptr->scale_y = y;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_size(wgr_handle_t sprite, float width, float height)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->width = width > 0.0f && height > 0.0f ? width : 0.0f;
    sprite_ptr->height = width > 0.0f && height > 0.0f ? height : 0.0f;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_nine_slice(wgr_handle_t sprite, float left, float top, float right, float bottom)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->slice_left = left > 0.0f ? left : 0.0f;
    sprite_ptr->slice_top = top > 0.0f ? top : 0.0f;
    sprite_ptr->slice_right = right > 0.0f ? right : 0.0f;
    sprite_ptr->slice_bottom = bottom > 0.0f ? bottom : 0.0f;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_pivot(wgr_handle_t sprite, float x, float y)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->pivot_x = x;
    sprite_ptr->pivot_y = y;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_tint(wgr_handle_t sprite, wgr_color_t color)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->tint = color;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_visible(wgr_handle_t sprite, bool visible)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->visible = visible;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_is_visible(wgr_handle_t sprite)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->visible;
}

WGRI_KEEP
bool wgr_sprite2d_set_pickable(wgr_handle_t sprite, bool pickable)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->pickable = pickable;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_is_pickable(wgr_handle_t sprite)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->pickable;
}

WGRI_KEEP
bool wgr_sprite2d_set_enabled(wgr_handle_t sprite, bool enabled)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->enabled = enabled;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_is_enabled(wgr_handle_t sprite)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && sprite_ptr->enabled;
}

WGRI_KEEP
bool wgr_sprite2d_set_pick_alpha_test(wgr_handle_t sprite, bool enable, float threshold)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) {
        return false;
    }
    sprite_ptr->alpha_test = enable;
    sprite_ptr->alpha_threshold = threshold;
    if (enable && sprite_ptr->texture != 0) {
        wgri_texture_ensure_alpha_mask(sprite_ptr->texture);
    }
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_alpha_mode(wgr_handle_t sprite, wgr_alpha_mode_t mode, float cutoff)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    if (sprite_ptr == NULL) return false;
    if (mode < WGR_ALPHA_OPAQUE || mode > WGR_ALPHA_ADD) {
        log_warn("wgr_sprite2d_set_alpha_mode: %d is not a wgr_alpha_mode_t", (int)mode);
        return false;
    }
    sprite_ptr->alpha_mode = mode;
    sprite_ptr->alpha_cutoff = cutoff < 0.0f ? 0.0f : cutoff > 1.0f ? 1.0f : cutoff;
    return true;
}

WGRI_KEEP
bool wgr_sprite2d_set_material(wgr_handle_t sprite, wgr_handle_t material)
{
    wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL && assign_material(&sprite_ptr->material, material, "wgr_sprite2d_set_material");
}

WGRI_KEEP
wgr_handle_t wgr_sprite2d_get_material(wgr_handle_t sprite)
{
    const wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL ? sprite_ptr->material : 0;
}

WGRI_KEEP
wgr_alpha_mode_t wgr_sprite2d_get_alpha_mode(wgr_handle_t sprite)
{
    const wgr_sprite2d_t *sprite_ptr = resolve(sprite);
    return sprite_ptr != NULL ? sprite_ptr->alpha_mode : WGR_ALPHA_BLEND;
}

WGRI_KEEP
void wgr_sprite2d_draw(wgr_handle_t sprite)
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

WGRI_KEEP
void wgr_texture_draw(wgr_handle_t texture, float x, float y, float width, float height, wgr_color_t tint)
{
    wgr_texture_draw_ex(texture, 0.0f, 0.0f, 0.0f, 0.0f, x, y, width, height, tint);
}

WGRI_KEEP
void wgr_texture_draw_ex(wgr_handle_t texture, float source_x, float source_y, float source_width, float source_height,
                        float x, float y, float width, float height, wgr_color_t tint)
{
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;
    float source[4], corners[8];

    if (texture == 0 || !wgri_texture_get_binding(texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
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
              (source[0] + source[2]) / (float)tw, (source[1] + source[3]) / (float)th, wgri_texture_is_flipped(texture),
              tint);
}

WGRI_KEEP
void wgr_texture_draw_nine_slice(wgr_handle_t texture, float source_x, float source_y, float source_width,
                                float source_height, float left, float top, float right, float bottom, float x,
                                float y, float width, float height, wgr_color_t tint)
{
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0;
    float source[4];
    const float slice[4] = {fmaxf(0.0f, left), fmaxf(0.0f, top), fmaxf(0.0f, right), fmaxf(0.0f, bottom)};
    const wgri_sprite2d_placement_t placement = {
        .x = x, .y = y, .width = width, .height = height, .scale_x = 1.0f, .scale_y = 1.0f,
    }; /* pivot (0, 0): (x, y) is the top-left corner */

    if (texture == 0 || width <= 0.0f || height <= 0.0f ||
        !wgri_texture_get_binding(texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    texture_source(source_x, source_y, source_width, source_height, tw, th, source);
    if (!draw_nine_slice(NULL, view, smp, wgri_texture_is_flipped(texture), source, tw, th, &placement, slice, tint)) {
        wgr_texture_draw_ex(texture, source[0], source[1], source[2], source[3], x, y, width, height, tint);
    }
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_sprite2d_module = {.name = "sprite2d", .order = 61, .init = wgri_sprite2d_init, .deinit = wgri_sprite2d_deinit};
WGRI_MODULE(wgr_sprite2d_module)
