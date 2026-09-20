#include "wgr_shape2d.h"

#include <math.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_pick_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_shape2d_internal.h"
#include "wgr_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

/* 2D shapes: immediate primitives in screen space, and retained shapes that are
 * scene 2D members (drawn over all 3D, picked by their exact area). Coordinates
 * are logical pixels, top-left origin, y down. The world's shapes are a separate
 * type (wgr_shape3d), like sprite2d/sprite3d and text2d/text3d. */

#define SHAPES2D_INITIAL 64 /* slots to start with; the pool doubles as needed */
#define WGR_CIRCLE_SEGMENTS 36
#define WGR_CORNER_SEGMENTS 8
#define SHAPE_PI ((float)M_PI)

typedef enum {
    WGR_SHAPE2D_NONE = 0,
    WGR_SHAPE2D_RECTANGLE = 1, /* dim: width, height, corner radius; bounds (0,0)-(w,h) */
    WGR_SHAPE2D_CIRCLE = 2,    /* dim: radius; bounds (-r,-r)-(r,r) */
    WGR_SHAPE2D_LINE = 3,      /* dim: x0, y0, x1, y1, thickness */
} wgr_shape2d_kind_t;

typedef struct {
    wgr_shape2d_kind_t kind;
    float dim[5];
    float x, y;
    float rotation; /* radians, clockwise on screen */
    float scale_x, scale_y;
    float pivot_x, pivot_y; /* 0..1 across the bounds; see pivot_offset */
    bool has_pivot;         /* false: the kind's own origin */
    float outline;          /* rectangles and circles: stroke thickness; 0 = filled */
    wgr_color_t color;
    bool visible;
    bool pickable;
    bool enabled; /* false: hits block the pointer but don't react (scene interaction) */
} wgr_shape2d_t;

static wgr_shape2d_t *wgr_shapes2d; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_shape2d_pool;

static void draw_2d(wgr_handle_t shape);
static bool pick_2d(wgr_handle_t shape, float screen_x, float screen_y, wgr_pick_result_t *out);

void wgr_shape2d_init(void)
{
    if (!wgr_handle_pool_init(&wgr_shape2d_pool, WGR_HANDLE_KIND_SHAPE2D, "shape2d", (void **)&wgr_shapes2d,
                             sizeof(wgr_shape2d_t), SHAPES2D_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("shape2d: out of memory");
    }
    wgr_scene_register_2d(WGR_HANDLE_KIND_SHAPE2D, draw_2d, pick_2d);
    wgr_scene_register_enabled(WGR_HANDLE_KIND_SHAPE2D, wgr_shape2d_is_enabled);
}

void wgr_shape2d_deinit(void)
{
    wgr_handle_pool_destroy(&wgr_shape2d_pool);
}

static wgr_shape2d_t *resolve(wgr_handle_t shape)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_shape2d_pool, shape, &index)) {
        if (shape != 0) {
            log_warn("Invalid shape2d handle (%u)", (unsigned int)shape);
        }
        return NULL;
    }
    return &wgr_shapes2d[index];
}

static void set_color(wgr_color_t color)
{
    wgr_colorf_t c = wgr_color_unpack(color);
    sgl_c4f(c.r, c.g, c.b, c.a);
}

/* --------------------------------------------------------- immediate 2D ---- */

WGR_KEEP
void wgr_shape2d_draw_rectangle(float x, float y, float width, float height, wgr_color_t color)
{
    const float x0 = x, y0 = y, x1 = x + width, y1 = y + height;

    sgl_begin_quads();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x0, y1);
    sgl_end();
}

WGR_KEEP
void wgr_shape2d_draw_rectangle_lines(float x, float y, float width, float height, wgr_color_t color)
{
    const float x0 = x, y0 = y, x1 = x + width, y1 = y + height;

    sgl_begin_line_strip();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x0, y1);
    sgl_v2f(x0, y0);
    sgl_end();
}

WGR_KEEP
void wgr_shape2d_draw_line(float start_x, float start_y, float end_x, float end_y, wgr_color_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v2f(start_x, start_y);
    sgl_v2f(end_x, end_y);
    sgl_end();
}

WGR_KEEP
void wgr_shape2d_draw_circle(float center_x, float center_y, float radius, wgr_color_t color)
{
    const float cx = center_x, cy = center_y;

    sgl_begin_triangles();
    set_color(color);
    for (int i = 0; i < WGR_CIRCLE_SEGMENTS; i++) {
        float a0 = (float)(2.0 * M_PI * i / WGR_CIRCLE_SEGMENTS);
        float a1 = (float)(2.0 * M_PI * (i + 1) / WGR_CIRCLE_SEGMENTS);
        sgl_v2f(cx, cy);
        sgl_v2f(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
        sgl_v2f(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
    }
    sgl_end();
}

WGR_KEEP
void wgr_shape2d_draw_circle_lines(float center_x, float center_y, float radius, wgr_color_t color)
{
    const float cx = center_x, cy = center_y;

    sgl_begin_line_strip();
    set_color(color);
    for (int i = 0; i <= WGR_CIRCLE_SEGMENTS; i++) {
        float a = (float)(2.0 * M_PI * i / WGR_CIRCLE_SEGMENTS);
        sgl_v2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
    }
    sgl_end();
}

WGR_KEEP
void wgr_shape2d_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2, wgr_color_t color)
{
    sgl_begin_triangles();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

/* ------------------------------------------------------- retained shapes ---- */

WGR_KEEP
wgr_handle_t wgr_shape2d_create(void)
{
    wgr_handle_t handle = wgr_handle_pool_alloc(&wgr_shape2d_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("shape2d: pool full (%u)", (unsigned)wgr_shape2d_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_shape2d_pool, handle, &index);
    wgr_shapes2d[index] = (wgr_shape2d_t){
        .kind = WGR_SHAPE2D_NONE,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .color = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

WGR_KEEP
void wgr_shape2d_destroy(wgr_handle_t shape)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return;
    }
    wgr_scene_forget(shape);
    *shape_ptr = (wgr_shape2d_t){0};
    wgr_handle_pool_free(&wgr_shape2d_pool, shape);
}

/* One shape kind with dim[] set. */
static bool set_kind(wgr_handle_t shape, wgr_shape2d_kind_t kind, const float *dim, int count)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = kind;
    memset(shape_ptr->dim, 0, sizeof(shape_ptr->dim));
    memcpy(shape_ptr->dim, dim, (size_t)count * sizeof(float));
    return true;
}

WGR_KEEP
bool wgr_shape2d_set_rectangle(wgr_handle_t shape, float width, float height, float corner_radius)
{
    const float radius = fmaxf(0.0f, fminf(corner_radius, fminf(width, height) * 0.5f));
    return set_kind(shape, WGR_SHAPE2D_RECTANGLE, (float[]){width, height, radius}, 3);
}

WGR_KEEP
bool wgr_shape2d_set_circle(wgr_handle_t shape, float radius)
{
    return set_kind(shape, WGR_SHAPE2D_CIRCLE, (float[]){radius}, 1);
}

WGR_KEEP
bool wgr_shape2d_set_line(wgr_handle_t shape, float x0, float y0, float x1, float y1, float thickness)
{
    return set_kind(shape, WGR_SHAPE2D_LINE, (float[]){x0, y0, x1, y1, thickness > 0.0f ? thickness : 1.0f}, 5);
}

WGR_KEEP
bool wgr_shape2d_set_transform(wgr_handle_t shape, float x, float y, float rotation, float scale_x, float scale_y)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->x = x;
    shape_ptr->y = y;
    shape_ptr->rotation = rotation;
    shape_ptr->scale_x = scale_x;
    shape_ptr->scale_y = scale_y;
    return true;
}

WGR_KEEP
bool wgr_shape2d_set_pivot(wgr_handle_t shape, float x, float y)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->pivot_x = x;
    shape_ptr->pivot_y = y;
    shape_ptr->has_pivot = true;
    return true;
}

WGR_KEEP
bool wgr_shape2d_set_outline(wgr_handle_t shape, float thickness)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->outline = thickness > 0.0f ? thickness : 0.0f;
    return true;
}

WGR_KEEP
bool wgr_shape2d_set_color(wgr_handle_t shape, wgr_color_t color)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->color = color;
    return true;
}

WGR_KEEP
bool wgr_shape2d_set_visible(wgr_handle_t shape, bool visible)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->visible = visible;
    return true;
}

WGR_KEEP
bool wgr_shape2d_is_visible(wgr_handle_t shape)
{
    const wgr_shape2d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->visible;
}

WGR_KEEP
bool wgr_shape2d_set_pickable(wgr_handle_t shape, bool pickable)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->pickable = pickable;
    return true;
}

WGR_KEEP
bool wgr_shape2d_is_pickable(wgr_handle_t shape)
{
    const wgr_shape2d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->pickable;
}

WGR_KEEP
bool wgr_shape2d_set_enabled(wgr_handle_t shape, bool enabled)
{
    wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->enabled = enabled;
    return true;
}

WGR_KEEP
bool wgr_shape2d_is_enabled(wgr_handle_t shape)
{
    const wgr_shape2d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->enabled;
}

/* ---------------------------------------------------------- geometry -------- */

/* Where the shape's local geometry sits once its pivot is applied: the local
 * translation that puts the pivot point on the shape's position. Without a pivot
 * (the default) the geometry keeps its own origin — a rectangle's top-left corner,
 * a circle's center — so nothing moves. Lines have explicit endpoints, so they
 * ignore the pivot. */
static void pivot_offset(const wgr_shape2d_t *shape_ptr, float *out_x, float *out_y)
{
    *out_x = 0.0f;
    *out_y = 0.0f;
    if (!shape_ptr->has_pivot) {
        return;
    }
    switch (shape_ptr->kind) {
        case WGR_SHAPE2D_RECTANGLE:
            *out_x = -shape_ptr->pivot_x * shape_ptr->dim[0];
            *out_y = -shape_ptr->pivot_y * shape_ptr->dim[1];
            break;
        case WGR_SHAPE2D_CIRCLE:
            *out_x = -(shape_ptr->pivot_x - 0.5f) * shape_ptr->dim[0] * 2.0f;
            *out_y = -(shape_ptr->pivot_y - 0.5f) * shape_ptr->dim[0] * 2.0f;
            break;
        default:
            break;
    }
}

/* The outline of the rectangle (x, y, width, height) with per-corner radii (top-left,
 * top-right, bottom-right, bottom-left), each clamped to half the shorter side,
 * clockwise from the top-left corner's arc: WGR_CORNER_SEGMENTS + 1 points per corner,
 * so any two outlines have the same count and can be joined into a band. */
int wgr_shape2d_rounded_outline(float x, float y, float width, float height, const float radii[4], float *xy)
{
    const float w = width > 0.0f ? width : 0.0f, h = height > 0.0f ? height : 0.0f;
    const float limit = fminf(w, h) * 0.5f;
    int n = 0;
    for (int c = 0; c < 4; c++) {
        /* corners: top-left, top-right, bottom-right, bottom-left (y down) */
        const float r = fmaxf(0.0f, fminf(radii[c], limit));
        const float cx = c == 0 || c == 3 ? x + r : x + w - r;
        const float cy = c == 0 || c == 1 ? y + r : y + h - r;
        const float start = SHAPE_PI + (float)c * SHAPE_PI * 0.5f; /* 180, 270, 0, 90 degrees */
        for (int i = 0; i <= WGR_CORNER_SEGMENTS; i++) {
            const float a = start + (float)i / WGR_CORNER_SEGMENTS * SHAPE_PI * 0.5f;
            xy[n * 2] = cx + cosf(a) * r;
            xy[n * 2 + 1] = cy + sinf(a) * r;
            n++;
        }
    }
    return n;
}

/* A retained rectangle's outline from its own origin, inset by `inset` (the radius
 * shrinks with it). */
static int rounded_rect_points(float width, float height, float radius, float inset, float *xy)
{
    const float r = fmaxf(0.0f, radius - inset);
    const float radii[4] = {r, r, r, r};
    return wgr_shape2d_rounded_outline(inset, inset, width - 2.0f * inset, height - 2.0f * inset, radii, xy);
}

static void fill_fan(float cx, float cy, const float *xy, int n)
{
    sgl_begin_triangles();
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        sgl_v2f(cx, cy);
        sgl_v2f(xy[i * 2], xy[i * 2 + 1]);
        sgl_v2f(xy[j * 2], xy[j * 2 + 1]);
    }
    sgl_end();
}

/* The band between two closed outlines with the same point count. */
static void fill_band(const float *outer, const float *inner, int n)
{
    sgl_begin_triangles();
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        sgl_v2f(outer[i * 2], outer[i * 2 + 1]);
        sgl_v2f(outer[j * 2], outer[j * 2 + 1]);
        sgl_v2f(inner[i * 2], inner[i * 2 + 1]);
        sgl_v2f(inner[i * 2], inner[i * 2 + 1]);
        sgl_v2f(outer[j * 2], outer[j * 2 + 1]);
        sgl_v2f(inner[j * 2], inner[j * 2 + 1]);
    }
    sgl_end();
}

#define OUTLINE_POINTS WGR_SHAPE2D_OUTLINE_POINTS
_Static_assert(WGR_SHAPE2D_OUTLINE_POINTS == 4 * (WGR_CORNER_SEGMENTS + 1), "outline size out of step");

WGR_KEEP
void wgr_shape2d_draw_rounded_rectangle(float x, float y, float width, float height, float r_top_left,
                                       float r_top_right, float r_bottom_right, float r_bottom_left,
                                       wgr_color_t color)
{
    const float radii[4] = {r_top_left, r_top_right, r_bottom_right, r_bottom_left};
    float xy[OUTLINE_POINTS * 2];
    const int n = wgr_shape2d_rounded_outline(x, y, width, height, radii, xy);
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }
    set_color(color);
    fill_fan(x + width * 0.5f, y + height * 0.5f, xy, n);
}

WGR_KEEP
void wgr_shape2d_draw_border(float x, float y, float width, float height, float left, float top, float right,
                            float bottom, float r_top_left, float r_top_right, float r_bottom_right,
                            float r_bottom_left, wgr_color_t color)
{
    const float l = fmaxf(0.0f, left), t = fmaxf(0.0f, top), r = fmaxf(0.0f, right), b = fmaxf(0.0f, bottom);
    const float outer_radii[4] = {r_top_left, r_top_right, r_bottom_right, r_bottom_left};
    /* inner corners are circular too: the outer radius less the wider adjoining side */
    const float inner_radii[4] = {r_top_left - fmaxf(l, t), r_top_right - fmaxf(r, t), r_bottom_right - fmaxf(r, b),
                                  r_bottom_left - fmaxf(l, b)};
    float outer[OUTLINE_POINTS * 2], inner[OUTLINE_POINTS * 2];
    int n;

    if (width <= 0.0f || height <= 0.0f || l + t + r + b <= 0.0f) {
        return;
    }
    n = wgr_shape2d_rounded_outline(x, y, width, height, outer_radii, outer);
    /* borders wider than the box fill it: the inner outline stays inside the outer one */
    wgr_shape2d_rounded_outline(x + fminf(l, width), y + fminf(t, height), width - l - r, height - t - b, inner_radii, inner);
    set_color(color);
    fill_band(outer, inner, n);
}

static int circle_points(float radius, float *xy)
{
    for (int i = 0; i < WGR_CIRCLE_SEGMENTS; i++) {
        const float a = (float)i / WGR_CIRCLE_SEGMENTS * 2.0f * SHAPE_PI;
        xy[i * 2] = cosf(a) * radius;
        xy[i * 2 + 1] = sinf(a) * radius;
    }
    return WGR_CIRCLE_SEGMENTS;
}

static void draw_shape(const wgr_shape2d_t *shape_ptr)
{
    float outer[4 * (WGR_CORNER_SEGMENTS + 1) * 2 + WGR_CIRCLE_SEGMENTS * 2];
    float inner[sizeof(outer) / sizeof(outer[0])];
    const float w = shape_ptr->dim[0], h = shape_ptr->dim[1];
    float ox, oy;
    int n;

    pivot_offset(shape_ptr, &ox, &oy);
    sgl_push_matrix();
    sgl_translate(shape_ptr->x, shape_ptr->y, 0.0f);
    sgl_rotate(shape_ptr->rotation, 0.0f, 0.0f, 1.0f);
    sgl_scale(shape_ptr->scale_x, shape_ptr->scale_y, 1.0f);
    sgl_translate(ox, oy, 0.0f);
    set_color(shape_ptr->color);
    switch (shape_ptr->kind) {
        case WGR_SHAPE2D_RECTANGLE:
            n = rounded_rect_points(w, h, shape_ptr->dim[2], 0.0f, outer);
            if (shape_ptr->outline > 0.0f) {
                rounded_rect_points(w, h, shape_ptr->dim[2], fminf(shape_ptr->outline, fminf(w, h) * 0.5f), inner);
                fill_band(outer, inner, n);
            } else {
                fill_fan(w * 0.5f, h * 0.5f, outer, n);
            }
            break;
        case WGR_SHAPE2D_CIRCLE:
            n = circle_points(shape_ptr->dim[0], outer);
            if (shape_ptr->outline > 0.0f) {
                circle_points(fmaxf(0.0f, shape_ptr->dim[0] - shape_ptr->outline), inner);
                fill_band(outer, inner, n);
            } else {
                fill_fan(0.0f, 0.0f, outer, n);
            }
            break;
        case WGR_SHAPE2D_LINE: {
            const float dx = shape_ptr->dim[2] - shape_ptr->dim[0], dy = shape_ptr->dim[3] - shape_ptr->dim[1];
            const float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.0f) {
                const float half = shape_ptr->dim[4] * 0.5f, nx = -dy / len * half, ny = dx / len * half;
                sgl_begin_quads();
                sgl_v2f(shape_ptr->dim[0] + nx, shape_ptr->dim[1] + ny);
                sgl_v2f(shape_ptr->dim[2] + nx, shape_ptr->dim[3] + ny);
                sgl_v2f(shape_ptr->dim[2] - nx, shape_ptr->dim[3] - ny);
                sgl_v2f(shape_ptr->dim[0] - nx, shape_ptr->dim[1] - ny);
                sgl_end();
            }
            break;
        }
        default:
            break;
    }
    sgl_pop_matrix();
}

WGR_KEEP
void wgr_shape2d_draw(wgr_handle_t shape)
{
    const wgr_shape2d_t *shape_ptr = resolve(shape);
    if (shape_ptr != NULL && shape_ptr->visible && shape_ptr->kind != WGR_SHAPE2D_NONE) {
        draw_shape(shape_ptr);
    }
}

static void draw_2d(wgr_handle_t shape)
{
    wgr_shape2d_draw(shape);
}

/* ------------------------------------------------------------- picking ----- */

/* Inside a rounded rectangle (0,0)-(w,h) inset by `inset`. */
static bool inside_rounded_rect(float px, float py, float w, float h, float radius, float inset)
{
    const float r = fmaxf(0.0f, radius - inset);
    const float x0 = inset, y0 = inset, x1 = w - inset, y1 = h - inset;
    float cx, cy;
    if (px < x0 || px > x1 || py < y0 || py > y1) {
        return false;
    }
    cx = px < x0 + r ? x0 + r : (px > x1 - r ? x1 - r : px);
    cy = py < y0 + r ? y0 + r : (py > y1 - r ? y1 - r : py);
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
}

static bool pick_2d(wgr_handle_t shape, float screen_x, float screen_y, wgr_pick_result_t *out)
{
    const wgr_shape2d_t *shape_ptr = resolve(shape);
    float px, py, dx, dy, c, s, ox, oy;
    bool hit = false;

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable ||
        shape_ptr->kind == WGR_SHAPE2D_NONE || shape_ptr->scale_x == 0.0f || shape_ptr->scale_y == 0.0f) {
        return false;
    }
    /* screen -> local: undo translation, rotation, scale, then the pivot */
    pivot_offset(shape_ptr, &ox, &oy);
    dx = screen_x - shape_ptr->x;
    dy = screen_y - shape_ptr->y;
    c = cosf(-shape_ptr->rotation);
    s = sinf(-shape_ptr->rotation);
    px = (dx * c - dy * s) / shape_ptr->scale_x - ox;
    py = (dx * s + dy * c) / shape_ptr->scale_y - oy;

    switch (shape_ptr->kind) {
        case WGR_SHAPE2D_RECTANGLE: {
            const float w = shape_ptr->dim[0], h = shape_ptr->dim[1], r = shape_ptr->dim[2];
            hit = inside_rounded_rect(px, py, w, h, r, 0.0f) &&
                  (shape_ptr->outline <= 0.0f ||
                   !inside_rounded_rect(px, py, w, h, r, fminf(shape_ptr->outline, fminf(w, h) * 0.5f)));
            break;
        }
        case WGR_SHAPE2D_CIRCLE: {
            const float d2 = px * px + py * py, r = shape_ptr->dim[0];
            const float inner = shape_ptr->outline > 0.0f ? fmaxf(0.0f, r - shape_ptr->outline) : 0.0f;
            hit = d2 <= r * r && (shape_ptr->outline <= 0.0f || d2 >= inner * inner);
            break;
        }
        case WGR_SHAPE2D_LINE: {
            const float ax = shape_ptr->dim[0], ay = shape_ptr->dim[1];
            const float bx = shape_ptr->dim[2] - ax, by = shape_ptr->dim[3] - ay;
            const float len2 = bx * bx + by * by;
            const float t = len2 > 0.0f ? fmaxf(0.0f, fminf(1.0f, ((px - ax) * bx + (py - ay) * by) / len2)) : 0.0f;
            const float qx = ax + bx * t - px, qy = ay + by * t - py;
            const float half = shape_ptr->dim[4] * 0.5f;
            hit = len2 > 0.0f && qx * qx + qy * qy <= half * half;
            break;
        }
        default:
            break;
    }
    if (!hit) {
        return false;
    }
    *out = (wgr_pick_result_t){
        .hit = true,
        .handle = shape,
        .point_world = {screen_x, screen_y, 0.0f},
        .point_local = {px, py, 0.0f},
        .normal_world = {0.0f, 0.0f, 1.0f},
        .normal_local = {0.0f, 0.0f, 1.0f},
    };
    return true;
}
