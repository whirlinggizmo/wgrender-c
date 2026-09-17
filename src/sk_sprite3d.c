#include "sk_sprite3d.h"

#include <math.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_pick.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite3d.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define MAX_SPRITES 1024

typedef struct {
    sk_handle_t texture;
    vec3_t position;
    vec3_t rotation;
    vec3_t scale;
    float size;
    int facing;
    sk_handle_t tint;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
    bool pick_alpha_test;
    float pick_alpha_threshold;
} sk_sprite3d_t;

static sk_sprite3d_t sk_sprites[MAX_SPRITES];
static sk_handle_pool_t sk_sprite_pool;
static uint16_t sk_sprite_free_indices[MAX_SPRITES];
static uint16_t sk_sprite_generations[MAX_SPRITES];
static unsigned char sk_sprite_occupied[MAX_SPRITES];

static void draw_handle(sk_handle_t handle);
static int collect_transparent(sk_handle_t handle, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items);
static void draw_transparent(sk_handle_t handle, int part);
static bool sprite_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model);
static bool sprite_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out);
static void sprite_quad_corners(const sk_sprite3d_t *sprite_ptr, const sk_camera3d_t *cam,
                                vec3_t *tl, vec3_t *tr, vec3_t *br, vec3_t *bl);

/* ---- tiny vec3 helpers ------------------------------------------------- */
static vec3_t v3_sub(vec3_t a, vec3_t b) { return (vec3_t){a.x - b.x, a.y - b.y, a.z - b.z}; }
static vec3_t v3_cross(vec3_t a, vec3_t b)
{
    return (vec3_t){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static vec3_t v3_norm(vec3_t a)
{
    float len = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len <= 1e-6f) return (vec3_t){0, 0, 0};
    return (vec3_t){a.x / len, a.y / len, a.z / len};
}

static sk_sprite3d_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_sprite_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid sprite3d handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &sk_sprites[index];
}

static sk_handle_t create_sprite(sk_handle_t texture)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_sprite_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SPRITES reached (%d)", MAX_SPRITES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_sprite_pool, handle, &index);
    sk_sprites[index] = (sk_sprite3d_t){
        .texture = texture,
        .scale = {1.0f, 1.0f, 1.0f},
        .size = 1.0f,
        .facing = SK_SPRITE3D_FACING_CAMERA,
        .tint = 0,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    if (texture != 0) {
        sk_texture_retain(texture);
    }
    return handle;
}

SK_KEEP
sk_handle_t sk_sprite3d_create(sk_handle_t texture)
{
    return create_sprite(texture);
}

SK_KEEP
bool sk_sprite3d_set_texture(sk_handle_t handle, sk_handle_t texture)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (sprite_ptr->texture == texture) {
        return true;
    }
    if (sprite_ptr->texture != 0) {
        sk_texture_release(sprite_ptr->texture);
    }
    sprite_ptr->texture = texture;
    if (texture != 0) {
        sk_texture_retain(texture);
    }
    return true;
}

SK_KEEP
bool sk_sprite3d_set_transform(sk_handle_t handle,
                               float px, float py, float pz,
                               float rx, float ry, float rz,
                               float sx, float sy, float sz)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->position = (vec3_t){px, py, pz};
    sprite_ptr->rotation = (vec3_t){rx, ry, rz};
    sprite_ptr->scale = (vec3_t){sx, sy, sz};
    return true;
}

SK_KEEP
bool sk_sprite3d_set_size(sk_handle_t handle, float size)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->size = size;
    return true;
}

SK_KEEP
bool sk_sprite3d_set_facing(sk_handle_t handle, sk_sprite3d_facing_t facing)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || facing < SK_SPRITE3D_FACING_CAMERA || facing > SK_SPRITE3D_FACING_FREE) return false;
    sprite_ptr->facing = facing;
    return true;
}

SK_KEEP
bool sk_sprite3d_set_pickable(sk_handle_t handle, bool pickable)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_sprite3d_is_pickable(sk_handle_t handle)
{
    const sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->pickable;
}

SK_KEEP
bool sk_sprite3d_set_enabled(sk_handle_t handle, bool enabled)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_sprite3d_is_enabled(sk_handle_t handle)
{
    const sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->enabled;
}

SK_KEEP
vec3_t sk_sprite3d_get_position(sk_handle_t handle)
{
    const sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->position : (vec3_t){0, 0, 0};
}

SK_KEEP
vec3_t sk_sprite3d_get_rotation(sk_handle_t handle)
{
    const sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->rotation : (vec3_t){0, 0, 0};
}

SK_KEEP
vec3_t sk_sprite3d_get_scale(sk_handle_t handle)
{
    const sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL ? sprite_ptr->scale : (vec3_t){0, 0, 0};
}

SK_KEEP
bool sk_sprite3d_set_tint(sk_handle_t handle, sk_handle_t color)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->tint = color;
    return true;
}

SK_KEEP
bool sk_sprite3d_set_visible(sk_handle_t handle, bool visible)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) return false;
    sprite_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_sprite3d_set_pick_alpha_test(sk_handle_t handle, bool enable, float threshold)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL) {
        return false;
    }
    if (enable && sprite_ptr->texture != 0) {
        sk_texture_ensure_alpha_mask(sprite_ptr->texture);
    }
    sprite_ptr->pick_alpha_test = enable;
    sprite_ptr->pick_alpha_threshold = threshold;
    return true;
}

SK_KEEP
bool sk_sprite3d_is_visible(sk_handle_t handle)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    return sprite_ptr != NULL && sprite_ptr->visible;
}

static void draw_handle(sk_handle_t handle)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    sk_camera3d_t cam;
    sg_view view;
    sg_sampler smp;
    color_t tint;
    float top_v;

    if (sprite_ptr == NULL || !sprite_ptr->visible) {
        return;
    }
    if (!sk_texture_get_binding(sprite_ptr->texture, &view, &smp, NULL, NULL)) {
        return;
    }
    top_v = sk_texture_is_flipped(sprite_ptr->texture) ? 1.0f : 0.0f; /* render target stored bottom-up */
    if (!sk_camera3d_get_active_data(&cam)) {
        return;
    }

    tint = sk_color_get(sprite_ptr->tint != 0 ? sprite_ptr->tint : 0); /* 0 -> white */

    {
        vec3_t tl, tr, br, bl;
        sprite_quad_corners(sprite_ptr, &cam, &tl, &tr, &br, &bl);

        sgl_enable_texture();
        sgl_texture(view, smp);
        sgl_begin_quads();
        sgl_c4f(tint.r, tint.g, tint.b, tint.a);
        sgl_v3f_t2f(tl.x, tl.y, tl.z, 0.0f, top_v);
        sgl_v3f_t2f(tr.x, tr.y, tr.z, 1.0f, top_v);
        sgl_v3f_t2f(br.x, br.y, br.z, 1.0f, 1.0f - top_v);
        sgl_v3f_t2f(bl.x, bl.y, bl.z, 0.0f, 1.0f - top_v);
        sgl_end();
        sgl_disable_texture();
    }
}

/* Scene: sprites are always in the transparent pass (textures usually have
 * alpha), sorted by their center. Direct sk_sprite3d_draw() calls keep the
 * depth-writing pipeline. */
static int collect_transparent(sk_handle_t handle, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    if (sprite_ptr == NULL || !sprite_ptr->visible || sprite_ptr->texture == 0 || max_items < 1) {
        return 0;
    }
    out[0] = (sk_transparent_item_t){
        .handle = handle,
        .part = 0,
        .depth = sk_scene_view_depth(cam, sprite_ptr->position),
    };
    return 1;
}

static void draw_transparent(sk_handle_t handle, int part)
{
    (void)part;
    draw_handle(handle);
}

SK_KEEP
void sk_sprite3d_draw(sk_handle_t handle)
{
    draw_handle(handle);
}

static bool sprite_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    float hw, hh, r;
    if (sprite_ptr == NULL || !sprite_ptr->visible) {
        return false;
    }
    /* The billboard rotates to face the camera, so use a conservative cube that
     * encloses the quad at any orientation (radius = half-diagonal). This keeps
     * the broadphase from rejecting a glancing hit on the rotated quad. */
    hw = 0.5f * sprite_ptr->size * sprite_ptr->scale.x;
    hh = 0.5f * sprite_ptr->size * sprite_ptr->scale.y;
    r = sqrtf(hw * hw + hh * hh);
    *lmin = (vec3_t){-r, -r, -r};
    *lmax = (vec3_t){r, r, r};
    *model = sk_mat4_translate(sprite_ptr->position.x, sprite_ptr->position.y, sprite_ptr->position.z);
    return true;
}

void sk_sprite3d_facing_basis(sk_sprite3d_facing_t facing, vec3_t rotation, const sk_camera3d_t *cam,
                              vec3_t *right, vec3_t *up)
{
    if (facing == SK_SPRITE3D_FACING_Y_UP) {
        *right = (vec3_t){1, 0, 0};
        *up = (vec3_t){0, 0, -1};
    } else if (facing == SK_SPRITE3D_FACING_FREE) {
        /* the quad lies in the sprite's local XY plane, turned by its rotation */
        const sk_mat4_t rot = sk_mat4_trs((vec3_t){0, 0, 0}, rotation, (vec3_t){1, 1, 1});
        *right = (vec3_t){rot.m[0], rot.m[1], rot.m[2]};
        *up = (vec3_t){rot.m[4], rot.m[5], rot.m[6]};
    } else {
        vec3_t fwd = v3_norm(v3_sub(cam->target, cam->position));
        vec3_t world_up = (facing == SK_SPRITE3D_FACING_CAMERA_FIXED_Y) ? (vec3_t){0, 1, 0} : cam->up;
        *right = v3_norm(v3_cross(fwd, world_up));
        *up = v3_norm(v3_cross(*right, fwd));
    }
}

/* The sprite's quad for this camera; drawing and picking both use it, so the quad
 * picked is the quad on screen. */
static void sprite_quad_corners(const sk_sprite3d_t *sprite_ptr, const sk_camera3d_t *cam,
                                vec3_t *tl, vec3_t *tr, vec3_t *br, vec3_t *bl)
{
    vec3_t right, up, c;
    float hw, hh;

    sk_sprite3d_facing_basis((sk_sprite3d_facing_t)sprite_ptr->facing, sprite_ptr->rotation, cam, &right, &up);

    hw = 0.5f * sprite_ptr->size * sprite_ptr->scale.x;
    hh = 0.5f * sprite_ptr->size * sprite_ptr->scale.y;
    c = sprite_ptr->position;
    *tl = (vec3_t){c.x - right.x * hw + up.x * hh, c.y - right.y * hw + up.y * hh, c.z - right.z * hw + up.z * hh};
    *tr = (vec3_t){c.x + right.x * hw + up.x * hh, c.y + right.y * hw + up.y * hh, c.z + right.z * hw + up.z * hh};
    *br = (vec3_t){c.x + right.x * hw - up.x * hh, c.y + right.y * hw - up.y * hh, c.z + right.z * hw - up.z * hh};
    *bl = (vec3_t){c.x - right.x * hw - up.x * hh, c.y - right.y * hw - up.y * hh, c.z - right.z * hw - up.z * hh};
}

static bool sprite_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    sk_camera3d_t cam;
    sk_ray_t ray;
    vec3_t tl, tr, br, bl;
    sk_ray_hit_t h0 = {0}, h1 = {0};
    const sk_ray_hit_t *best = NULL;
    bool got0, got1;

    if (out == NULL || sprite_ptr == NULL || !sprite_ptr->visible || !sprite_ptr->pickable) {
        return false;
    }
    if (!sk_camera3d_get_active_data(&cam)) {
        return false;
    }

    sprite_quad_corners(sprite_ptr, &cam, &tl, &tr, &br, &bl);

    ray.origin = origin;
    ray.dir = dir;
    /* The quad is built in world space (its orientation comes from the camera),
     * so the triangle hits are world-space. A billboard has no stable local
     * frame, so local space is just the sprite's translation. */
    got0 = sk_pick_ray_triangle(ray, tl, tr, br, &h0);
    got1 = sk_pick_ray_triangle(ray, tl, br, bl, &h1);

    if (got0 && (!got1 || h0.t <= h1.t)) {
        best = &h0;
    } else if (got1) {
        best = &h1;
    }

    /* Optional alpha test: reject the hit if the texel under it is transparent,
     * so the ray passes through to whatever is behind the sprite. UVs follow the
     * quad layout in draw_handle(): tl=(0,0) tr=(1,0) br=(1,1) bl=(0,1). */
    if (best != NULL && sprite_ptr->pick_alpha_test) {
        float uv_x, uv_y, a;
        if (best == &h0) { /* tri (tl,tr,br): uv = (u+v, v) */
            uv_x = best->u + best->v;
            uv_y = best->v;
        } else { /* tri (tl,br,bl): uv = (u, u+v) */
            uv_x = best->u;
            uv_y = best->u + best->v;
        }
        if (sk_texture_sample_alpha(sprite_ptr->texture, uv_x, uv_y, &a) &&
            a < sprite_ptr->pick_alpha_threshold) {
            best = NULL;
        }
    }

    if (best != NULL) {
        sk_mat4_t model = sk_mat4_translate(sprite_ptr->position.x, sprite_ptr->position.y, sprite_ptr->position.z);
        sk_pick_result_from_world(best, ray, model, out);
    } else {
        *out = (sk_pick_result_t){0};
    }
    return true;
}

SK_KEEP
void sk_sprite3d_destroy(sk_handle_t handle)
{
    sk_sprite3d_t *sprite_ptr = resolve(handle);
    sk_handle_t texture;
    if (sprite_ptr == NULL) {
        return;
    }
    texture = sprite_ptr->texture;
    *sprite_ptr = (sk_sprite3d_t){0};
    sk_handle_pool_free(&sk_sprite_pool, handle);
    if (texture != 0) {
        sk_texture_release(texture);
    }
}

void sk_sprite3d_init(void)
{
    memset(sk_sprites, 0, sizeof(sk_sprites));
    sk_handle_pool_init(&sk_sprite_pool,
                        SK_HANDLE_KIND_SPRITE3D,
                        MAX_SPRITES,
                        sk_sprite_free_indices,
                        MAX_SPRITES,
                        sk_sprite_generations,
                        sk_sprite_occupied);
    sk_scene_register_passes(SK_HANDLE_KIND_SPRITE3D, NULL, collect_transparent, draw_transparent);
    sk_scene_register_bounds(SK_HANDLE_KIND_SPRITE3D, sprite_bounds);
    sk_scene_register_pick(SK_HANDLE_KIND_SPRITE3D, sprite_pick);
    sk_scene_register_enabled(SK_HANDLE_KIND_SPRITE3D, sk_sprite3d_is_enabled);
}

void sk_sprite3d_deinit(void)
{
    sk_handle_pool_reset(&sk_sprite_pool);
}
