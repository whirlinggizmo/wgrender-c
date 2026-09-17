#include "sk_text3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_font.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_pick.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite3d.h"
#include "sk_logger.h"
#include "sk_text.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define MAX_TEXT3D 512
#define RASTER_SIZE 64.0f /* glyphs are rasterized at this pixel size and scaled to world units */

/* Text3d object: a string placed in 3D, drawn with a TrueType font. */
typedef struct {
    sk_handle_t font;
    char *text; /* owned copy, may be NULL */
    float size; /* world units: line height */
    vec3_t position;
    vec3_t rotation; /* radians, for SK_SPRITE3D_FACING_FREE */
    sk_sprite3d_facing_t facing;
    sk_handle_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} sk_text3d_t;

static sk_text3d_t sk_text3ds[MAX_TEXT3D];
static sk_handle_pool_t sk_text3d_pool;
static uint16_t sk_text3d_free_indices[MAX_TEXT3D];
static uint16_t sk_text3d_generations[MAX_TEXT3D];
static unsigned char sk_text3d_occupied[MAX_TEXT3D];
static sgl_pipeline sk_text3d_pipeline; /* fontstash's glyph shader, depth-tested */
static sg_shader sk_text3d_pipeline_shader;

static sk_text3d_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_text3d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &sk_text3ds[index];
}

/* ---------------------------------------------------------------- layout ---- */

/* Set up fontstash for `font` at the raster size; false if the font isn't ready. */
static bool use_font(sk_handle_t font)
{
    FONScontext *fons = sk_font_context();
    const int id = sk_font_fons_id(sk_text_resolve_font(font)); /* 0: the default font */
    if (fons == NULL || id == FONS_INVALID) {
        return false;
    }
    fonsSetFont(fons, id);
    fonsSetSize(fons, RASTER_SIZE);
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

/* Text extent in world units for `size`, or (0, 0) if the font isn't ready. */
static vec2_t text_extent(sk_handle_t font, const char *text, float size)
{
    float bounds[4] = {0};
    if (text == NULL || text[0] == '\0' || !use_font(font)) {
        return (vec2_t){0, 0};
    }
    fonsTextBounds(sk_font_context(), 0.0f, 0.0f, text, NULL, bounds);
    const float scale = size / RASTER_SIZE;
    return (vec2_t){(bounds[2] - bounds[0]) * scale, (bounds[3] - bounds[1]) * scale};
}

/* The text's quad corners in world space (centered on position). */
static void quad_corners(vec3_t center, vec3_t right, vec3_t up, vec2_t extent, vec3_t *tl, vec3_t *tr, vec3_t *br,
                         vec3_t *bl)
{
    const vec3_t r = sk_v3_scale(right, extent.x * 0.5f), u = sk_v3_scale(up, extent.y * 0.5f);
    *tl = (vec3_t){center.x - r.x + u.x, center.y - r.y + u.y, center.z - r.z + u.z};
    *tr = (vec3_t){center.x + r.x + u.x, center.y + r.y + u.y, center.z + r.z + u.z};
    *br = (vec3_t){center.x + r.x - u.x, center.y + r.y - u.y, center.z + r.z - u.z};
    *bl = (vec3_t){center.x - r.x - u.x, center.y - r.y - u.y, center.z - r.z - u.z};
}

/* ------------------------------------------------------------------ draw ---- */

/* Draw text centered at `center`, laid out along right (x) and up (y), in the
 * current 3D projection. */
static void draw_text(sk_handle_t font, const char *text, float size, vec3_t center, vec3_t right, vec3_t up,
                      sk_handle_t color)
{
    FONScontext *fons = sk_font_context();
    FONStextIter iter;
    FONSquad quad;
    sg_view atlas;
    sg_sampler sampler;
    sg_shader shader;
    float bounds[4] = {0};

    if (text == NULL || text[0] == '\0' || !use_font(font) ||
        !sk_fontstash_render_state(fons, &atlas, &sampler, &shader)) {
        return;
    }
    if (sk_text3d_pipeline.id == SG_INVALID_ID || sk_text3d_pipeline_shader.id != shader.id) {
        if (sk_text3d_pipeline.id != SG_INVALID_ID) {
            sgl_destroy_pipeline(sk_text3d_pipeline);
        }
        sk_text3d_pipeline = sgl_make_pipeline(&(sg_pipeline_desc){
            .shader = shader,
            .depth = {.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = false},
            .colors[0].blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            },
            .label = "sk-text3d-pip",
        });
        sk_text3d_pipeline_shader = shader;
    }

    fonsTextBounds(fons, 0.0f, 0.0f, text, NULL, bounds);
    const float scale = size / RASTER_SIZE;
    const float ox = -(bounds[0] + bounds[2]) * 0.5f, oy = -(bounds[1] + bounds[3]) * 0.5f; /* center the text */
    const color_t c = sk_color_get(color);
    const sk_mat4_t model = {{
        right.x * scale, right.y * scale, right.z * scale, 0.0f,
        -up.x * scale, -up.y * scale, -up.z * scale, 0.0f, /* text y runs down; world up runs up */
        0.0f, 0.0f, 0.0f, 0.0f,
        center.x, center.y, center.z, 1.0f,
    }};

    sgl_push_matrix();
    sgl_mult_matrix(model.m);
    sgl_push_pipeline();
    sgl_load_pipeline(sk_text3d_pipeline);
    sgl_enable_texture();
    sgl_texture(atlas, sampler);
    sgl_begin_triangles();
    sgl_c4f(c.r, c.g, c.b, c.a);
    fonsTextIterInit(fons, &iter, ox, oy, text, NULL);
    while (fonsTextIterNext(fons, &iter, &quad)) {
        sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y0, quad.s1, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
        sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
        sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
        sgl_v2f_t2f(quad.x0, quad.y1, quad.s0, quad.t1);
    }
    sgl_end();
    sgl_disable_texture();
    sgl_pop_pipeline();
    sgl_pop_matrix();
}

static void draw_handle(sk_handle_t handle)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    sk_camera3d_t cam;
    vec3_t right, up;

    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL || !sk_camera3d_get_active_data(&cam)) {
        return;
    }
    sk_sprite3d_facing_basis(text_ptr->facing, text_ptr->rotation, &cam, &right, &up);
    draw_text(text_ptr->font, text_ptr->text, text_ptr->size, text_ptr->position, right, up, text_ptr->color);
}

/* Scene passes: glyph edges blend, so text is always a transparent part. */
static int collect_transparent(sk_handle_t handle, const sk_camera3d_t *cam, sk_transparent_item_t *out,
                               int max_items)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL || max_items < 1) {
        return 0;
    }
    out[0] = (sk_transparent_item_t){
        .handle = handle,
        .part = 0,
        .depth = sk_scene_view_depth(cam, text_ptr->position),
    };
    return 1;
}

static void draw_transparent(sk_handle_t handle, int part)
{
    (void)part;
    draw_handle(handle);
}

/* ------------------------------------------------------------------ pick ---- */

static bool text_bounds(sk_handle_t handle, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL) {
        return false;
    }
    const vec2_t extent = text_extent(text_ptr->font, text_ptr->text, text_ptr->size);
    if (extent.x <= 0.0f) {
        return false;
    }
    /* any facing fits in a sphere around the center */
    const float r = 0.5f * sqrtf(extent.x * extent.x + extent.y * extent.y);
    *lmin = (vec3_t){-r, -r, -r};
    *lmax = (vec3_t){r, r, r};
    *model = sk_mat4_translate(text_ptr->position.x, text_ptr->position.y, text_ptr->position.z);
    return true;
}

static bool text_pick(sk_handle_t handle, vec3_t origin, vec3_t dir, sk_pick_result_t *out)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    sk_camera3d_t cam;
    vec3_t right, up, tl, tr, br, bl;
    sk_ray_hit_t h0 = {0}, h1 = {0};
    const sk_ray_t ray = {origin, dir};

    if (out == NULL || text_ptr == NULL || !text_ptr->visible || !text_ptr->pickable || text_ptr->text == NULL ||
        !sk_camera3d_get_active_data(&cam)) {
        return false;
    }
    sk_sprite3d_facing_basis(text_ptr->facing, text_ptr->rotation, &cam, &right, &up);
    quad_corners(text_ptr->position, right, up, text_extent(text_ptr->font, text_ptr->text, text_ptr->size), &tl, &tr,
                 &br, &bl);
    const bool got0 = sk_pick_ray_triangle(ray, tl, tr, br, &h0);
    const bool got1 = sk_pick_ray_triangle(ray, tl, br, bl, &h1);
    const sk_ray_hit_t *best = got0 && (!got1 || h0.t <= h1.t) ? &h0 : (got1 ? &h1 : NULL);
    if (best != NULL) {
        sk_pick_result_from_world(best, ray,
                                  sk_mat4_translate(text_ptr->position.x, text_ptr->position.y, text_ptr->position.z),
                                  out);
    } else {
        *out = (sk_pick_result_t){0};
    }
    return true;
}

/* ------------------------------------------------------------ public API ---- */

SK_KEEP
sk_handle_t sk_text3d_create(sk_handle_t font)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_text3d_pool);
    uint16_t index = 0;
    if (handle == 0) {
        log_error("MAX_TEXT3D reached (%d)", MAX_TEXT3D);
        return 0;
    }
    sk_handle_pool_resolve(&sk_text3d_pool, handle, &index);
    sk_font_retain(font); /* no-op for 0 */
    sk_text3ds[index] = (sk_text3d_t){
        .font = font,
        .size = 1.0f,
        .facing = SK_SPRITE3D_FACING_CAMERA,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

SK_KEEP
void sk_text3d_destroy(sk_handle_t handle)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return;
    free(text_ptr->text);
    sk_font_release(text_ptr->font);
    memset(text_ptr, 0, sizeof(*text_ptr));
    sk_handle_pool_free(&sk_text3d_pool, handle);
}

SK_KEEP
bool sk_text3d_set_font(sk_handle_t handle, sk_handle_t font)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    sk_font_retain(font); /* before releasing, in case they're the same font */
    sk_font_release(text_ptr->font);
    text_ptr->font = font;
    return true;
}

SK_KEEP
bool sk_text3d_set_text(sk_handle_t handle, const char *text)
{
    sk_text3d_t *text_ptr = resolve(handle);
    char *copy = NULL;
    if (text_ptr == NULL) return false;
    if (text != NULL) {
        const size_t n = strlen(text);
        copy = (char *)malloc(n + 1);
        if (copy == NULL) return false;
        memcpy(copy, text, n + 1);
    }
    free(text_ptr->text);
    text_ptr->text = copy;
    return true;
}

SK_KEEP
bool sk_text3d_set_size(sk_handle_t handle, float size)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || size <= 0.0f) return false;
    text_ptr->size = size;
    return true;
}

SK_KEEP
bool sk_text3d_set_transform(sk_handle_t handle, float x, float y, float z, float rx, float ry, float rz)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->position = (vec3_t){x, y, z};
    text_ptr->rotation = (vec3_t){rx, ry, rz};
    return true;
}

SK_KEEP
bool sk_text3d_set_facing(sk_handle_t handle, sk_sprite3d_facing_t facing)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || facing < SK_SPRITE3D_FACING_CAMERA || facing > SK_SPRITE3D_FACING_FREE) return false;
    text_ptr->facing = facing;
    return true;
}

SK_KEEP
bool sk_text3d_set_color(sk_handle_t handle, sk_handle_t color)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->color = color;
    return true;
}

SK_KEEP
bool sk_text3d_set_visible(sk_handle_t handle, bool visible)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_text3d_is_visible(sk_handle_t handle)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->visible;
}

SK_KEEP
bool sk_text3d_set_pickable(sk_handle_t handle, bool pickable)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_text3d_is_pickable(sk_handle_t handle)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->pickable;
}

SK_KEEP
bool sk_text3d_set_enabled(sk_handle_t handle, bool enabled)
{
    sk_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_text3d_is_enabled(sk_handle_t handle)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->enabled;
}

SK_KEEP
vec2_t sk_text3d_get_size(sk_handle_t handle)
{
    const sk_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL ? text_extent(text_ptr->font, text_ptr->text, text_ptr->size) : (vec2_t){0, 0};
}

SK_KEEP
void sk_text3d_draw(sk_handle_t handle)
{
    draw_handle(handle);
}

SK_KEEP
void sk_text_draw_3d(sk_handle_t font, const char *text, float x, float y, float z, float size, sk_handle_t color)
{
    sk_camera3d_t cam;
    vec3_t right, up;
    if (!sk_camera3d_get_active_data(&cam)) {
        return;
    }
    sk_sprite3d_facing_basis(SK_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
    draw_text(font, text, size, (vec3_t){x, y, z}, right, up, color);
}

void sk_text3d_init(void)
{
    memset(sk_text3ds, 0, sizeof(sk_text3ds));
    sk_handle_pool_init(&sk_text3d_pool, SK_HANDLE_KIND_TEXT3D, MAX_TEXT3D, sk_text3d_free_indices, MAX_TEXT3D,
                        sk_text3d_generations, sk_text3d_occupied);
    sk_scene_register_passes(SK_HANDLE_KIND_TEXT3D, NULL, collect_transparent, draw_transparent);
    sk_scene_register_bounds(SK_HANDLE_KIND_TEXT3D, text_bounds);
    sk_scene_register_pick(SK_HANDLE_KIND_TEXT3D, text_pick);
    sk_scene_register_enabled(SK_HANDLE_KIND_TEXT3D, sk_text3d_is_enabled);
}

void sk_text3d_deinit(void)
{
    for (int i = 0; i < MAX_TEXT3D; i++) {
        if (sk_text3d_occupied[i]) sk_font_release(sk_text3ds[i].font);
        free(sk_text3ds[i].text);
    }
    memset(sk_text3ds, 0, sizeof(sk_text3ds));
    if (sk_text3d_pipeline.id != SG_INVALID_ID) {
        sgl_destroy_pipeline(sk_text3d_pipeline);
    }
    sk_text3d_pipeline = (sgl_pipeline){0};
    sk_text3d_pipeline_shader = (sg_shader){0};
    sk_handle_pool_reset(&sk_text3d_pool);
}
