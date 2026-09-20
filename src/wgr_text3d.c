#include "wgr_text3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/wgr_color.h"
#include "wgr_color.h"
#include "internal/wgr_camera3d.h"
#include "internal/wgr_font.h"
#include "internal/wgr_handle_pool.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_math.h"
#include "internal/wgr_pick.h"
#include "internal/wgr_scene.h"
#include "internal/wgr_sprite3d.h"
#include "internal/wgr_module.h"
#include "wgr_logger.h"
#include "wgr_text.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define TEXT3D_INITIAL 64 /* slots to start with; the pool doubles as needed */
#define RASTER_SIZE 64.0f /* glyphs are rasterized at this pixel size and scaled to world units */

/* Text3d object: a string placed in 3D, drawn with a TrueType font. */
typedef struct {
    wgr_handle_t font;
    char *text; /* owned copy, may be NULL */
    float size; /* world units: line height */
    vec3_t position;
    vec3_t rotation; /* radians, for WGR_SPRITE3D_FACING_FREE */
    wgr_sprite3d_facing_t facing;
    float max_width;                  /* world units; 0 = no wrap */
    wgr_text_align_t align_x, align_y; /* where `position` sits on the block */
    wgr_color_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} wgr_text3d_t;

static wgr_text3d_t *wgr_text3ds; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_text3d_pool;
static sgl_pipeline wgr_text3d_pipeline; /* fontstash's glyph shader, depth-tested */
static sg_shader wgr_text3d_pipeline_shader;

static wgr_text3d_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_text3d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid text3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &wgr_text3ds[index];
}

/* ---------------------------------------------------------------- layout ---- */

/* Set up fontstash for `font` at the raster size; false if the font isn't ready. */
static bool use_font(wgr_handle_t font)
{
    FONScontext *fons = wgr_font_context();
    const int id = wgr_font_fons_id(wgr_text_resolve_font(font)); /* 0: the default font */
    if (fons == NULL || id == FONS_INVALID) {
        return false;
    }
    fonsSetFont(fons, id);
    fonsSetSize(fons, RASTER_SIZE);
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

#define MAX_TEXT3D_LINES 64

/* The text laid out at the raster size: one entry per line, plus the block's own
 * size. Lines break at newlines and, with max_width set, between words — the same
 * splitting text2d uses (wgr_text_split_lines), since text3d draws its own glyphs. */
typedef struct {
    const char *starts[MAX_TEXT3D_LINES];
    const char *ends[MAX_TEXT3D_LINES];
    float widths[MAX_TEXT3D_LINES];
    int count;
    float width;       /* widest line, raster units */
    float line_height; /* raster units */
} text3d_block_t;

static bool layout_block(wgr_handle_t font, const char *text, float size, float max_width, text3d_block_t *out)
{
    FONScontext *fons = wgr_font_context();
    float ascender = 0.0f, descender = 0.0f, bounds[4] = {0};
    const float to_raster = size > 0.0f ? RASTER_SIZE / size : 0.0f;

    if (text == NULL || text[0] == '\0' || to_raster <= 0.0f || !use_font(font)) {
        return false;
    }
    out->count = wgr_text_split_lines(text, -1, max_width > 0.0f ? max_width * to_raster : 0.0f, out->starts, out->ends,
                                     MAX_TEXT3D_LINES);
    if (out->count == 0) {
        return false;
    }
    fonsVertMetrics(fons, &ascender, &descender, &out->line_height);
    out->width = 0.0f;
    for (int i = 0; i < out->count; i++) {
        out->widths[i] = fonsTextBounds(fons, 0.0f, 0.0f, out->starts[i], out->ends[i], bounds);
        if (out->widths[i] > out->width) {
            out->width = out->widths[i];
        }
    }
    return true;
}

/* Block size in world units, or (0, 0) if the font isn't ready. */
static vec2_t text_extent(wgr_handle_t font, const char *text, float size, float max_width)
{
    text3d_block_t block;
    if (!layout_block(font, text, size, max_width, &block)) {
        return (vec2_t){0, 0};
    }
    const float scale = size / RASTER_SIZE;
    return (vec2_t){block.width * scale, (float)block.count * block.line_height * scale};
}

/* Where the block's center sits: `position` is its left/center/right and
 * top/middle/bottom edge, per the alignment. */
static vec3_t block_center(vec3_t position, vec3_t right, vec3_t up, vec2_t extent, wgr_text_align_t align_x,
                           wgr_text_align_t align_y)
{
    const float dx = align_x == WGR_TEXT_ALIGN_LEFT ? extent.x * 0.5f
                     : align_x == WGR_TEXT_ALIGN_RIGHT ? -extent.x * 0.5f
                                                      : 0.0f;
    const float dy = align_y == WGR_TEXT_ALIGN_TOP ? -extent.y * 0.5f
                     : align_y == WGR_TEXT_ALIGN_BOTTOM ? extent.y * 0.5f
                                                       : 0.0f;
    return (vec3_t){position.x + right.x * dx + up.x * dy, position.y + right.y * dx + up.y * dy,
                    position.z + right.z * dx + up.z * dy};
}

/* The text's quad corners in world space (centered on the block's center). */
static void quad_corners(vec3_t center, vec3_t right, vec3_t up, vec2_t extent, vec3_t *tl, vec3_t *tr, vec3_t *br,
                         vec3_t *bl)
{
    const vec3_t r = wgr_v3_scale(right, extent.x * 0.5f), u = wgr_v3_scale(up, extent.y * 0.5f);
    *tl = (vec3_t){center.x - r.x + u.x, center.y - r.y + u.y, center.z - r.z + u.z};
    *tr = (vec3_t){center.x + r.x + u.x, center.y + r.y + u.y, center.z + r.z + u.z};
    *br = (vec3_t){center.x + r.x - u.x, center.y + r.y - u.y, center.z + r.z - u.z};
    *bl = (vec3_t){center.x - r.x - u.x, center.y - r.y - u.y, center.z - r.z - u.z};
}

/* ------------------------------------------------------------------ draw ---- */

/* Draw text centered at `center`, laid out along right (x) and up (y), in the
 * current 3D projection. */
static void draw_text(wgr_handle_t font, const char *text, float size, float max_width, wgr_text_align_t align_x,
                      wgr_text_align_t align_y, vec3_t position, vec3_t right, vec3_t up, wgr_color_t color)
{
    FONScontext *fons = wgr_font_context();
    FONStextIter iter;
    FONSquad quad;
    sg_view atlas;
    sg_sampler sampler;
    sg_shader shader;
    text3d_block_t block;

    if (!layout_block(font, text, size, max_width, &block) ||
        !wgr_fontstash_render_state(fons, &atlas, &sampler, &shader)) {
        return;
    }
    if (wgr_text3d_pipeline.id == SG_INVALID_ID || wgr_text3d_pipeline_shader.id != shader.id) {
        if (wgr_text3d_pipeline.id != SG_INVALID_ID) {
            sgl_destroy_pipeline(wgr_text3d_pipeline);
        }
        wgr_text3d_pipeline = sgl_make_pipeline(&(sg_pipeline_desc){
            .shader = shader,
            .depth = {.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = false},
            .colors[0].blend = {
                .enabled = true,
                .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
                .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            },
            .label = "wgr-text3d-pip",
        });
        wgr_text3d_pipeline_shader = shader;
    }

    const float scale = size / RASTER_SIZE;
    const float block_height = (float)block.count * block.line_height;
    const vec2_t extent = {block.width * scale, block_height * scale};
    const vec3_t center = block_center(position, right, up, extent, align_x, align_y);
    const wgr_colorf_t c = wgr_color_unpack(color);
    const wgr_mat4_t model = {{
        right.x * scale, right.y * scale, right.z * scale, 0.0f,
        -up.x * scale, -up.y * scale, -up.z * scale, 0.0f, /* text y runs down; world up runs up */
        0.0f, 0.0f, 0.0f, 0.0f,
        center.x, center.y, center.z, 1.0f,
    }};

    sgl_push_matrix();
    sgl_mult_matrix(model.m);
    sgl_push_pipeline();
    sgl_load_pipeline(wgr_text3d_pipeline);
    sgl_enable_texture();
    sgl_texture(atlas, sampler);
    sgl_begin_triangles();
    sgl_c4f(c.r, c.g, c.b, c.a);
    /* the model matrix puts (0, 0) at the block's center, x right and y down */
    for (int i = 0; i < block.count; i++) {
        const float indent = align_x == WGR_TEXT_ALIGN_CENTER   ? (block.width - block.widths[i]) * 0.5f
                             : align_x == WGR_TEXT_ALIGN_RIGHT ? block.width - block.widths[i]
                                                              : 0.0f;
        const float ox = -block.width * 0.5f + indent;
        const float oy = -block_height * 0.5f + (float)i * block.line_height;
        fonsTextIterInit(fons, &iter, ox, oy, block.starts[i], block.ends[i]);
        while (fonsTextIterNext(fons, &iter, &quad)) {
            sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
            sgl_v2f_t2f(quad.x1, quad.y0, quad.s1, quad.t0);
            sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
            sgl_v2f_t2f(quad.x0, quad.y0, quad.s0, quad.t0);
            sgl_v2f_t2f(quad.x1, quad.y1, quad.s1, quad.t1);
            sgl_v2f_t2f(quad.x0, quad.y1, quad.s0, quad.t1);
        }
    }
    sgl_end();
    sgl_disable_texture();
    sgl_pop_pipeline();
    sgl_pop_matrix();
}

static void draw_handle(wgr_handle_t handle)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    wgr_camera3d_t cam;
    vec3_t right, up;

    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL || !wgr_camera3d_get_active_data(&cam)) {
        return;
    }
    wgr_sprite3d_facing_basis(text_ptr->facing, text_ptr->rotation, &cam, &right, &up);
    draw_text(text_ptr->font, text_ptr->text, text_ptr->size, text_ptr->max_width, text_ptr->align_x,
              text_ptr->align_y, text_ptr->position, right, up, text_ptr->color);
}

/* Scene passes: glyph edges blend, so text is always a transparent part. */
static int collect_transparent(wgr_handle_t handle, const wgr_camera3d_t *cam, wgr_transparent_item_t *out,
                               int max_items)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL || max_items < 1) {
        return 0;
    }
    out[0] = (wgr_transparent_item_t){
        .handle = handle,
        .part = 0,
        .depth = wgr_scene_view_depth(cam, text_ptr->position),
    };
    return 1;
}

static void draw_transparent(wgr_handle_t handle, int part)
{
    (void)part;
    draw_handle(handle);
}

/* ------------------------------------------------------------------ pick ---- */

static bool text_bounds(wgr_handle_t handle, vec3_t *lmin, vec3_t *lmax, wgr_mat4_t *model)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || !text_ptr->visible || text_ptr->text == NULL) {
        return false;
    }
    const vec2_t extent = text_extent(text_ptr->font, text_ptr->text, text_ptr->size, text_ptr->max_width);
    if (extent.x <= 0.0f) {
        return false;
    }
    /* any facing fits in a sphere around the center */
    const float r = 0.5f * sqrtf(extent.x * extent.x + extent.y * extent.y);
    *lmin = (vec3_t){-r, -r, -r};
    *lmax = (vec3_t){r, r, r};
    *model = wgr_mat4_translate(text_ptr->position.x, text_ptr->position.y, text_ptr->position.z);
    return true;
}

static bool text_pick(wgr_handle_t handle, vec3_t origin, vec3_t dir, wgr_pick_result_t *out)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    wgr_camera3d_t cam;
    vec3_t right, up, tl, tr, br, bl;
    wgr_ray_hit_t h0 = {0}, h1 = {0};
    const wgr_ray_t ray = {origin, dir};

    if (out == NULL || text_ptr == NULL || !text_ptr->visible || !text_ptr->pickable || text_ptr->text == NULL ||
        !wgr_camera3d_get_active_data(&cam)) {
        return false;
    }
    wgr_sprite3d_facing_basis(text_ptr->facing, text_ptr->rotation, &cam, &right, &up);
    const vec2_t extent = text_extent(text_ptr->font, text_ptr->text, text_ptr->size, text_ptr->max_width);
    quad_corners(block_center(text_ptr->position, right, up, extent, text_ptr->align_x, text_ptr->align_y), right, up,
                 extent, &tl, &tr, &br, &bl);
    const bool got0 = wgr_pick_ray_triangle(ray, tl, tr, br, &h0);
    const bool got1 = wgr_pick_ray_triangle(ray, tl, br, bl, &h1);
    const wgr_ray_hit_t *best = got0 && (!got1 || h0.t <= h1.t) ? &h0 : (got1 ? &h1 : NULL);
    if (best != NULL) {
        wgr_pick_result_from_world(best, ray,
                                  wgr_mat4_translate(text_ptr->position.x, text_ptr->position.y, text_ptr->position.z),
                                  out);
    } else {
        *out = (wgr_pick_result_t){0};
    }
    return true;
}

/* ------------------------------------------------------------ public API ---- */

WGR_KEEP
wgr_handle_t wgr_text3d_create(wgr_handle_t font)
{
    wgr_handle_t handle = wgr_handle_pool_alloc(&wgr_text3d_pool);
    uint16_t index = 0;
    if (handle == 0) {
        log_error("text3d: pool full (%u)", (unsigned)wgr_text3d_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_text3d_pool, handle, &index);
    wgr_font_retain(font); /* no-op for 0 */
    wgr_text3ds[index] = (wgr_text3d_t){
        .font = font,
        .size = 1.0f,
        .facing = WGR_SPRITE3D_FACING_CAMERA,
        .align_x = WGR_TEXT_ALIGN_CENTER,
        .align_y = WGR_TEXT_ALIGN_MIDDLE,
        .color = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

WGR_KEEP
void wgr_text3d_destroy(wgr_handle_t handle)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return;
    wgr_scene_forget(handle);
    free(text_ptr->text);
    wgr_font_release(text_ptr->font);
    memset(text_ptr, 0, sizeof(*text_ptr));
    wgr_handle_pool_free(&wgr_text3d_pool, handle);
}

WGR_KEEP
bool wgr_text3d_set_font(wgr_handle_t handle, wgr_handle_t font)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    wgr_font_retain(font); /* before releasing, in case they're the same font */
    wgr_font_release(text_ptr->font);
    text_ptr->font = font;
    return true;
}

WGR_KEEP
bool wgr_text3d_set_text(wgr_handle_t handle, const char *text)
{
    wgr_text3d_t *text_ptr = resolve(handle);
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

WGR_KEEP
bool wgr_text3d_set_size(wgr_handle_t handle, float size)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || size <= 0.0f) return false;
    text_ptr->size = size;
    return true;
}

WGR_KEEP
WGR_KEEP
bool wgr_text3d_set_align(wgr_handle_t handle, wgr_text_align_t horizontal, wgr_text_align_t vertical)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    if (horizontal > WGR_TEXT_ALIGN_RIGHT || vertical < WGR_TEXT_ALIGN_TOP || vertical > WGR_TEXT_ALIGN_BOTTOM) {
        log_warn("wgr_text3d_set_align: horizontal is LEFT/CENTER/RIGHT, vertical TOP/MIDDLE/BOTTOM");
        return false;
    }
    text_ptr->align_x = horizontal;
    text_ptr->align_y = vertical;
    return true;
}

WGR_KEEP
bool wgr_text3d_set_max_width(wgr_handle_t handle, float width)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->max_width = width > 0.0f ? width : 0.0f;
    return true;
}

WGR_KEEP
bool wgr_text3d_set_transform(wgr_handle_t handle, float x, float y, float z, float rx, float ry, float rz)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->position = (vec3_t){x, y, z};
    text_ptr->rotation = (vec3_t){rx, ry, rz};
    return true;
}

WGR_KEEP
bool wgr_text3d_set_facing(wgr_handle_t handle, wgr_sprite3d_facing_t facing)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL || facing < WGR_SPRITE3D_FACING_CAMERA || facing > WGR_SPRITE3D_FACING_FREE) return false;
    text_ptr->facing = facing;
    return true;
}

WGR_KEEP
bool wgr_text3d_set_color(wgr_handle_t handle, wgr_color_t color)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->color = color;
    return true;
}

WGR_KEEP
bool wgr_text3d_set_visible(wgr_handle_t handle, bool visible)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->visible = visible;
    return true;
}

WGR_KEEP
bool wgr_text3d_is_visible(wgr_handle_t handle)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->visible;
}

WGR_KEEP
bool wgr_text3d_set_pickable(wgr_handle_t handle, bool pickable)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->pickable = pickable;
    return true;
}

WGR_KEEP
bool wgr_text3d_is_pickable(wgr_handle_t handle)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->pickable;
}

WGR_KEEP
bool wgr_text3d_set_enabled(wgr_handle_t handle, bool enabled)
{
    wgr_text3d_t *text_ptr = resolve(handle);
    if (text_ptr == NULL) return false;
    text_ptr->enabled = enabled;
    return true;
}

WGR_KEEP
bool wgr_text3d_is_enabled(wgr_handle_t handle)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL && text_ptr->enabled;
}

WGR_KEEP
vec2_t wgr_text3d_get_size(wgr_handle_t handle)
{
    const wgr_text3d_t *text_ptr = resolve(handle);
    return text_ptr != NULL ? text_extent(text_ptr->font, text_ptr->text, text_ptr->size, text_ptr->max_width)
                            : (vec2_t){0, 0};
}

WGR_KEEP
void wgr_text3d_draw(wgr_handle_t handle)
{
    draw_handle(handle);
}

WGR_KEEP
void wgr_text_draw_3d(wgr_handle_t font, const char *text, float x, float y, float z, float size, wgr_color_t color)
{
    wgr_camera3d_t cam;
    vec3_t right, up;
    if (!wgr_camera3d_get_active_data(&cam)) {
        return;
    }
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
    draw_text(font, text, size, 0.0f, WGR_TEXT_ALIGN_CENTER, WGR_TEXT_ALIGN_MIDDLE, (vec3_t){x, y, z}, right,
              up, color);
}

void wgr_text3d_init(void)
{
    if (!wgr_handle_pool_init(&wgr_text3d_pool, WGR_HANDLE_KIND_TEXT3D, "text3d", (void **)&wgr_text3ds,
                             sizeof(wgr_text3d_t), TEXT3D_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("text3d: out of memory");
    }
    wgr_scene_register_passes(WGR_HANDLE_KIND_TEXT3D, NULL, collect_transparent, draw_transparent);
    wgr_scene_register_bounds(WGR_HANDLE_KIND_TEXT3D, text_bounds);
    wgr_scene_register_pick(WGR_HANDLE_KIND_TEXT3D, text_pick);
    wgr_scene_register_enabled(WGR_HANDLE_KIND_TEXT3D, wgr_text3d_is_enabled);
}

void wgr_text3d_deinit(void)
{
    for (int i = 0; i < wgr_text3d_pool.capacity; i++) {
        if (wgr_text3d_pool.occupied[i]) wgr_font_release(wgr_text3ds[i].font);
        free(wgr_text3ds[i].text);
    }
    if (wgr_text3d_pipeline.id != SG_INVALID_ID) {
        sgl_destroy_pipeline(wgr_text3d_pipeline);
    }
    wgr_text3d_pipeline = (sgl_pipeline){0};
    wgr_text3d_pipeline_shader = (sg_shader){0};
    wgr_handle_pool_destroy(&wgr_text3d_pool);
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_text3d_module = {.name = "text3d", .order = 81, .init = wgr_text3d_init, .deinit = wgr_text3d_deinit};
WGR_MODULE(wgr_text3d_module)
