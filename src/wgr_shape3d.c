#include "wgr_shape3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_color_internal.h"
#include "wgr_color.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_pick_internal.h"
#include "internal/wgr_scene_internal.h"
#include "wgr_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define WGR_CIRCLE_SEGMENTS 36

#define SHAPES3D_INITIAL 64 /* slots to start with; the pool doubles as needed */
#define MAX_STRIP_POINTS 65536

typedef enum {
    WGR_SHAPE3D_NONE = 0,
    WGR_SHAPE3D_CUBE = 1,
    WGR_SHAPE3D_SPHERE = 2,
    WGR_SHAPE3D_RECTANGLE = 3,  /* filled, XY plane */
    WGR_SHAPE3D_CIRCLE = 4,     /* outline, XY plane */
    WGR_SHAPE3D_LINE = 5,
    WGR_SHAPE3D_LINE_STRIP = 6,
} wgr_shape3d_kind_t;

typedef struct {
    wgr_shape3d_kind_t kind;
    float dim[6];   /* cube: w,h,l; sphere/circle: radius; rectangle: w,h; line: x0,y0,z0,x1,y1,z1 */
    vec3_t *points; /* line strip */
    int point_count;
    int point_capacity;
    vec3_t position;
    vec3_t rotation; /* radians */
    vec3_t scale;
    wgr_color_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} wgr_shape3d_t;

static wgr_shape3d_t *wgr_shapes3d; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_shape3d_pool;

static void draw_handle(wgr_handle_t shape);
static void draw_opaque(wgr_handle_t shape);
static int collect_transparent(wgr_handle_t shape, const wgri_camera3d_t *cam,
                               wgri_transparent_item_t *out, int max_items);
static void draw_transparent(wgr_handle_t shape, int part);
static bool shape_bounds(wgr_handle_t shape, vec3_t *lmin, vec3_t *lmax, wgri_mat4_t *model);
static bool shape_pick(wgr_handle_t shape, vec3_t origin, vec3_t dir, wgr_pick_result_t *out);

void wgri_shape3d_init(void)
{
    if (!wgri_handle_pool_init(&wgr_shape3d_pool, WGR_HANDLE_KIND_SHAPE3D, "shape3d", (void **)&wgr_shapes3d,
                             sizeof(wgr_shape3d_t), SHAPES3D_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("shape3d: out of memory");
    }
    wgri_scene_register_passes(WGR_HANDLE_KIND_SHAPE3D, draw_opaque, collect_transparent, draw_transparent);
    wgri_scene_register_bounds(WGR_HANDLE_KIND_SHAPE3D, shape_bounds);
    wgri_scene_register_pick(WGR_HANDLE_KIND_SHAPE3D, shape_pick);
    wgri_scene_register_enabled(WGR_HANDLE_KIND_SHAPE3D, wgr_shape3d_is_enabled);
}

void wgri_shape3d_deinit(void)
{
    for (int i = 0; i < wgr_shape3d_pool.capacity; i++) {
        free(wgr_shapes3d[i].points);
    }
    wgri_handle_pool_destroy(&wgr_shape3d_pool);
}

static void set_color(wgr_color_t color)
{
    wgri_colorf_t c = wgri_color_unpack(color);
    sgl_c4f(c.r, c.g, c.b, c.a);
}

WGRI_KEEP
void wgr_shape3d_draw_line(float x0, float y0, float z0,
                           float x1, float y1, float z1, wgr_color_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v3f(x0, y0, z0);
    sgl_v3f(x1, y1, z1);
    sgl_end();
}

WGRI_KEEP
void wgr_shape3d_draw_cube(float cx, float cy, float cz,
                        float width, float height, float length, wgr_color_t color)
{
    const float x0 = cx - width * 0.5f, x1 = cx + width * 0.5f;
    const float y0 = cy - height * 0.5f, y1 = cy + height * 0.5f;
    const float z0 = cz - length * 0.5f, z1 = cz + length * 0.5f;

    sgl_begin_quads();
    set_color(color);
    /* +z / -z */
    sgl_v3f(x0, y0, z1); sgl_v3f(x1, y0, z1); sgl_v3f(x1, y1, z1); sgl_v3f(x0, y1, z1);
    sgl_v3f(x1, y0, z0); sgl_v3f(x0, y0, z0); sgl_v3f(x0, y1, z0); sgl_v3f(x1, y1, z0);
    /* +x / -x */
    sgl_v3f(x1, y0, z1); sgl_v3f(x1, y0, z0); sgl_v3f(x1, y1, z0); sgl_v3f(x1, y1, z1);
    sgl_v3f(x0, y0, z0); sgl_v3f(x0, y0, z1); sgl_v3f(x0, y1, z1); sgl_v3f(x0, y1, z0);
    /* +y / -y */
    sgl_v3f(x0, y1, z1); sgl_v3f(x1, y1, z1); sgl_v3f(x1, y1, z0); sgl_v3f(x0, y1, z0);
    sgl_v3f(x0, y0, z0); sgl_v3f(x1, y0, z0); sgl_v3f(x1, y0, z1); sgl_v3f(x0, y0, z1);
    sgl_end();
}

WGRI_KEEP
void wgr_shape3d_draw_cube_wires(float cx, float cy, float cz,
                              float width, float height, float length, wgr_color_t color)
{
    const float x0 = cx - width * 0.5f, x1 = cx + width * 0.5f;
    const float y0 = cy - height * 0.5f, y1 = cy + height * 0.5f;
    const float z0 = cz - length * 0.5f, z1 = cz + length * 0.5f;

    sgl_begin_lines();
    set_color(color);
    /* bottom rectangle */
    sgl_v3f(x0, y0, z0); sgl_v3f(x1, y0, z0);
    sgl_v3f(x1, y0, z0); sgl_v3f(x1, y0, z1);
    sgl_v3f(x1, y0, z1); sgl_v3f(x0, y0, z1);
    sgl_v3f(x0, y0, z1); sgl_v3f(x0, y0, z0);
    /* top rectangle */
    sgl_v3f(x0, y1, z0); sgl_v3f(x1, y1, z0);
    sgl_v3f(x1, y1, z0); sgl_v3f(x1, y1, z1);
    sgl_v3f(x1, y1, z1); sgl_v3f(x0, y1, z1);
    sgl_v3f(x0, y1, z1); sgl_v3f(x0, y1, z0);
    /* verticals */
    sgl_v3f(x0, y0, z0); sgl_v3f(x0, y1, z0);
    sgl_v3f(x1, y0, z0); sgl_v3f(x1, y1, z0);
    sgl_v3f(x1, y0, z1); sgl_v3f(x1, y1, z1);
    sgl_v3f(x0, y0, z1); sgl_v3f(x0, y1, z1);
    sgl_end();
}

WGRI_KEEP
void wgr_shape3d_draw_sphere(float cx, float cy, float cz, float radius, wgr_color_t color)
{
    const int rings = 16;
    const int sectors = 24;

    sgl_begin_triangles();
    set_color(color);
    for (int r = 0; r < rings; r++) {
        float phi0 = (float)(M_PI * r / rings) - (float)(M_PI * 0.5);
        float phi1 = (float)(M_PI * (r + 1) / rings) - (float)(M_PI * 0.5);
        for (int s = 0; s < sectors; s++) {
            float th0 = (float)(2.0 * M_PI * s / sectors);
            float th1 = (float)(2.0 * M_PI * (s + 1) / sectors);

            float p00x = cx + radius * cosf(phi0) * cosf(th0);
            float p00y = cy + radius * sinf(phi0);
            float p00z = cz + radius * cosf(phi0) * sinf(th0);
            float p01x = cx + radius * cosf(phi0) * cosf(th1);
            float p01y = p00y;
            float p01z = cz + radius * cosf(phi0) * sinf(th1);
            float p10x = cx + radius * cosf(phi1) * cosf(th0);
            float p10y = cy + radius * sinf(phi1);
            float p10z = cz + radius * cosf(phi1) * sinf(th0);
            float p11x = cx + radius * cosf(phi1) * cosf(th1);
            float p11y = p10y;
            float p11z = cz + radius * cosf(phi1) * sinf(th1);

            sgl_v3f(p00x, p00y, p00z); sgl_v3f(p10x, p10y, p10z); sgl_v3f(p11x, p11y, p11z);
            sgl_v3f(p00x, p00y, p00z); sgl_v3f(p11x, p11y, p11z); sgl_v3f(p01x, p01y, p01z);
        }
    }
    sgl_end();
}

/* Local transform for an immediate 3D primitive: center, then euler radians. */
static void push_placement(float cx, float cy, float cz, float rx, float ry, float rz)
{
    sgl_push_matrix();
    sgl_translate(cx, cy, cz);
    sgl_rotate(rz, 0.0f, 0.0f, 1.0f);
    sgl_rotate(ry, 0.0f, 1.0f, 0.0f);
    sgl_rotate(rx, 1.0f, 0.0f, 0.0f);
}

static void rectangle_xy(float width, float height, wgr_color_t color)
{
    const float hw = width * 0.5f, hh = height * 0.5f;
    sgl_begin_quads();
    set_color(color);
    sgl_v3f(-hw, -hh, 0.0f); sgl_v3f(hw, -hh, 0.0f); sgl_v3f(hw, hh, 0.0f); sgl_v3f(-hw, hh, 0.0f);
    sgl_end();
}

static void circle_xy(float radius, wgr_color_t color)
{
    sgl_begin_line_strip();
    set_color(color);
    for (int i = 0; i <= WGR_CIRCLE_SEGMENTS; i++) {
        const float a = (float)(2.0 * M_PI * i / WGR_CIRCLE_SEGMENTS);
        sgl_v3f(cosf(a) * radius, sinf(a) * radius, 0.0f);
    }
    sgl_end();
}

WGRI_KEEP
void wgr_shape3d_draw_rectangle(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, wgr_color_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    rectangle_xy(width, height, color);
    sgl_pop_matrix();
}

WGRI_KEEP
void wgr_shape3d_draw_circle(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, wgr_color_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    circle_xy(radius, color);
    sgl_pop_matrix();
}

WGRI_KEEP
void wgr_shape3d_draw_grid(int slices, float spacing, wgr_color_t color)
{
    const float half = slices * spacing * 0.5f;

    sgl_begin_lines();
    set_color(color);
    for (int i = 0; i <= slices; i++) {
        float p = -half + i * spacing;
        sgl_v3f(p, 0.0f, -half); sgl_v3f(p, 0.0f, half);
        sgl_v3f(-half, 0.0f, p); sgl_v3f(half, 0.0f, p);
    }
    sgl_end();
}

/* -------------------------------------------------- retained 3D shapes ----- */

static wgr_shape3d_t *resolve(wgr_handle_t shape)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_shape3d_pool, shape, &index)) {
        if (shape != 0) {
            log_warn("Invalid shape handle (%u)", (unsigned int)shape);
        }
        return NULL;
    }
    return &wgr_shapes3d[index];
}

WGRI_KEEP
wgr_handle_t wgr_shape3d_create(void)
{
    wgr_handle_t handle = wgri_handle_pool_alloc(&wgr_shape3d_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("shape3d: pool full (%u)", (unsigned)wgr_shape3d_pool.max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_shape3d_pool, handle, &index);
    wgr_shapes3d[index] = (wgr_shape3d_t){
        .kind = WGR_SHAPE3D_NONE,
        .dim = {1.0f, 1.0f, 1.0f},
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .color = WGR_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

WGRI_KEEP
void wgr_shape3d_destroy(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return;
    }
    wgri_scene_forget(shape);
    free(shape_ptr->points);
    *shape_ptr = (wgr_shape3d_t){0};
    wgri_handle_pool_free(&wgr_shape3d_pool, shape);
}

WGRI_KEEP
bool wgr_shape3d_set_cube(wgr_handle_t shape, float width, float height, float length)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_CUBE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    shape_ptr->dim[2] = length;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_sphere(wgr_handle_t shape, float radius)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_SPHERE;
    shape_ptr->dim[0] = radius;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_rectangle(wgr_handle_t shape, float width, float height)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_RECTANGLE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_circle(wgr_handle_t shape, float radius)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_CIRCLE;
    shape_ptr->dim[0] = radius;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_line(wgr_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_LINE;
    shape_ptr->dim[0] = x0; shape_ptr->dim[1] = y0; shape_ptr->dim[2] = z0;
    shape_ptr->dim[3] = x1; shape_ptr->dim[4] = y1; shape_ptr->dim[5] = z1;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_line_strip(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = WGR_SHAPE3D_LINE_STRIP;
    shape_ptr->point_count = 0; /* keeps the allocation for rebuilding */
    return true;
}

WGRI_KEEP
bool wgr_shape3d_add_point(wgr_handle_t shape, float x, float y, float z)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || shape_ptr->kind != WGR_SHAPE3D_LINE_STRIP) {
        if (shape_ptr != NULL) log_warn("wgr_shape3d_add_point: shape isn't a line strip (wgr_shape3d_set_line_strip)");
        return false;
    }
    if (shape_ptr->point_count >= MAX_STRIP_POINTS) {
        log_warn("wgr_shape3d_add_point: line strip is full (%d points)", MAX_STRIP_POINTS);
        return false;
    }
    if (shape_ptr->point_count == shape_ptr->point_capacity) {
        const int capacity = shape_ptr->point_capacity > 0 ? shape_ptr->point_capacity * 2 : 16;
        vec3_t *grown = (vec3_t *)realloc(shape_ptr->points, (size_t)capacity * sizeof(vec3_t));
        if (grown == NULL) {
            return false;
        }
        shape_ptr->points = grown;
        shape_ptr->point_capacity = capacity;
    }
    shape_ptr->points[shape_ptr->point_count++] = (vec3_t){x, y, z};
    return true;
}

WGRI_KEEP
int wgr_shape3d_get_point_count(wgr_handle_t shape)
{
    const wgr_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->kind == WGR_SHAPE3D_LINE_STRIP ? shape_ptr->point_count : 0;
}

WGRI_KEEP
bool wgr_shape3d_set_transform(wgr_handle_t shape,
                              float position_x, float position_y, float position_z,
                              float rotation_x, float rotation_y, float rotation_z,
                              float scale_x, float scale_y, float scale_z)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->position = (vec3_t){position_x, position_y, position_z};
    shape_ptr->rotation = (vec3_t){rotation_x, rotation_y, rotation_z};
    shape_ptr->scale = (vec3_t){scale_x, scale_y, scale_z};
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_position(wgr_handle_t handle, float x, float y, float z)
{
    wgr_shape3d_t *shape_ptr = resolve(handle);
    if (shape_ptr == NULL) return false;
    shape_ptr->position = (vec3_t){x, y, z};
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_rotation(wgr_handle_t handle, float x, float y, float z)
{
    wgr_shape3d_t *shape_ptr = resolve(handle);
    if (shape_ptr == NULL) return false;
    shape_ptr->rotation = (vec3_t){x, y, z};
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_scale(wgr_handle_t handle, float x, float y, float z)
{
    wgr_shape3d_t *shape_ptr = resolve(handle);
    if (shape_ptr == NULL) return false;
    shape_ptr->scale = (vec3_t){x, y, z};
    return true;
}

WGRI_KEEP
vec3_t wgr_shape3d_get_position(wgr_handle_t handle)
{
    const wgr_shape3d_t *shape_ptr = resolve(handle);
    return shape_ptr != NULL ? shape_ptr->position : (vec3_t){0, 0, 0};
}

WGRI_KEEP
vec3_t wgr_shape3d_get_rotation(wgr_handle_t handle)
{
    const wgr_shape3d_t *shape_ptr = resolve(handle);
    return shape_ptr != NULL ? shape_ptr->rotation : (vec3_t){0, 0, 0};
}

WGRI_KEEP
vec3_t wgr_shape3d_get_scale(wgr_handle_t handle)
{
    const wgr_shape3d_t *shape_ptr = resolve(handle);
    return shape_ptr != NULL ? shape_ptr->scale : (vec3_t){0, 0, 0};
}

WGRI_KEEP
bool wgr_shape3d_set_color(wgr_handle_t shape, wgr_color_t color)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->color = color;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_set_visible(wgr_handle_t shape, bool visible)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->visible = visible;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_is_visible(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->visible;
}

WGRI_KEEP
bool wgr_shape3d_set_pickable(wgr_handle_t shape, bool pickable)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->pickable = pickable;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_is_pickable(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->pickable;
}

WGRI_KEEP
bool wgr_shape3d_set_enabled(wgr_handle_t shape, bool enabled)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->enabled = enabled;
    return true;
}

WGRI_KEEP
bool wgr_shape3d_is_enabled(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->enabled;
}

static void draw_handle(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == WGR_SHAPE3D_NONE) {
        return;
    }
    sgl_push_matrix();
    sgl_translate(shape_ptr->position.x, shape_ptr->position.y, shape_ptr->position.z);
    sgl_rotate(shape_ptr->rotation.z, 0.0f, 0.0f, 1.0f);
    sgl_rotate(shape_ptr->rotation.y, 0.0f, 1.0f, 0.0f);
    sgl_rotate(shape_ptr->rotation.x, 1.0f, 0.0f, 0.0f);
    sgl_scale(shape_ptr->scale.x, shape_ptr->scale.y, shape_ptr->scale.z);

    switch (shape_ptr->kind) {
        case WGR_SHAPE3D_CUBE:
            wgr_shape3d_draw_cube(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2], shape_ptr->color);
            break;
        case WGR_SHAPE3D_SPHERE:
            wgr_shape3d_draw_sphere(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->color);
            break;
        case WGR_SHAPE3D_RECTANGLE:
            rectangle_xy(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->color);
            break;
        case WGR_SHAPE3D_CIRCLE:
            circle_xy(shape_ptr->dim[0], shape_ptr->color);
            break;
        case WGR_SHAPE3D_LINE:
            wgr_shape3d_draw_line(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2],
                                  shape_ptr->dim[3], shape_ptr->dim[4], shape_ptr->dim[5], shape_ptr->color);
            break;
        case WGR_SHAPE3D_LINE_STRIP:
            if (shape_ptr->point_count >= 2) {
                sgl_begin_line_strip();
                set_color(shape_ptr->color);
                for (int i = 0; i < shape_ptr->point_count; i++) {
                    sgl_v3f(shape_ptr->points[i].x, shape_ptr->points[i].y, shape_ptr->points[i].z);
                }
                sgl_end();
            }
            break;
        default:
            break;
    }

    sgl_pop_matrix();
}

/* Scene passes: a shape is opaque unless its color is translucent. */
static bool is_translucent(wgr_handle_t shape)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && wgri_color_unpack(shape_ptr->color).a < 1.0f;
}

static void draw_opaque(wgr_handle_t shape)
{
    if (!is_translucent(shape)) {
        draw_handle(shape);
    }
}

static int collect_transparent(wgr_handle_t shape, const wgri_camera3d_t *cam,
                               wgri_transparent_item_t *out, int max_items)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == WGR_SHAPE3D_NONE ||
        !is_translucent(shape) || max_items < 1) {
        return 0;
    }
    out[0] = (wgri_transparent_item_t){
        .handle = shape,
        .part = 0,
        .depth = wgri_scene_view_depth(cam, shape_ptr->position),
    };
    return 1;
}

static void draw_transparent(wgr_handle_t shape, int part)
{
    (void)part;
    draw_handle(shape);
}

WGRI_KEEP
void wgr_shape3d_draw(wgr_handle_t shape)
{
    draw_handle(shape);
}

static bool shape_bounds(wgr_handle_t shape, vec3_t *lmin, vec3_t *lmax, wgri_mat4_t *model)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    float hx, hy, hz;
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == WGR_SHAPE3D_NONE) {
        return false;
    }
    switch (shape_ptr->kind) {
        case WGR_SHAPE3D_CUBE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = shape_ptr->dim[2] * 0.5f;
            break;
        case WGR_SHAPE3D_RECTANGLE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = 0.0f;
            break;
        case WGR_SHAPE3D_CIRCLE:
            hx = hy = shape_ptr->dim[0]; hz = 0.0f;
            break;
        case WGR_SHAPE3D_LINE:
        case WGR_SHAPE3D_LINE_STRIP: {
            const int count = shape_ptr->kind == WGR_SHAPE3D_LINE ? 2 : shape_ptr->point_count;
            if (count == 0) return false;
            *lmin = (vec3_t){1e30f, 1e30f, 1e30f};
            *lmax = (vec3_t){-1e30f, -1e30f, -1e30f};
            for (int i = 0; i < count; i++) {
                const vec3_t p = shape_ptr->kind == WGR_SHAPE3D_LINE
                                     ? (vec3_t){shape_ptr->dim[i * 3], shape_ptr->dim[i * 3 + 1], shape_ptr->dim[i * 3 + 2]}
                                     : shape_ptr->points[i];
                lmin->x = fminf(lmin->x, p.x); lmin->y = fminf(lmin->y, p.y); lmin->z = fminf(lmin->z, p.z);
                lmax->x = fmaxf(lmax->x, p.x); lmax->y = fmaxf(lmax->y, p.y); lmax->z = fmaxf(lmax->z, p.z);
            }
            *model = wgri_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
            return true;
        }
        default: /* sphere: dim[0] = radius */
            hx = hy = hz = shape_ptr->dim[0];
            break;
    }
    *lmin = (vec3_t){-hx, -hy, -hz};
    *lmax = (vec3_t){hx, hy, hz};
    *model = wgri_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
    return true;
}

static bool shape_pick(wgr_handle_t shape, vec3_t origin, vec3_t dir, wgr_pick_result_t *out)
{
    wgr_shape3d_t *shape_ptr = resolve(shape);
    wgri_mat4_t model;
    wgri_ray_t world, local;
    wgri_ray_hit_t h = {0};
    vec3_t lmin, lmax;
    bool hit;

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable || shape_ptr->kind == WGR_SHAPE3D_NONE) {
        return false;
    }

    model = wgri_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
    world.origin = origin;
    world.dir = dir;
    local = wgri_pick_ray_to_local(model, world);

    if (shape_ptr->kind == WGR_SHAPE3D_CUBE) {
        float hx = shape_ptr->dim[0] * 0.5f;
        float hy = shape_ptr->dim[1] * 0.5f;
        float hz = shape_ptr->dim[2] * 0.5f;
        lmin = (vec3_t){-hx, -hy, -hz};
        lmax = (vec3_t){hx, hy, hz};
        hit = wgri_pick_ray_aabb(local, lmin, lmax, &h);
    } else if (shape_ptr->kind == WGR_SHAPE3D_RECTANGLE || shape_ptr->kind == WGR_SHAPE3D_CIRCLE) {
        /* the XY plane (both sides); circles are picked anywhere inside the outline */
        hit = false;
        if (fabsf(local.dir.z) > 1e-6f) { /* parallel (edge-on): no area to hit */
            const float t = -local.origin.z / local.dir.z;
            const float px = local.origin.x + local.dir.x * t, py = local.origin.y + local.dir.y * t;
            const bool inside = shape_ptr->kind == WGR_SHAPE3D_RECTANGLE
                                    ? fabsf(px) <= shape_ptr->dim[0] * 0.5f && fabsf(py) <= shape_ptr->dim[1] * 0.5f
                                    : px * px + py * py <= shape_ptr->dim[0] * shape_ptr->dim[0];
            if (t >= 0.0f && inside) {
                h = (wgri_ray_hit_t){.hit = true, .t = t, .point = {px, py, 0.0f},
                                   .normal = {0.0f, 0.0f, local.dir.z < 0.0f ? 1.0f : -1.0f}};
                hit = true;
            }
        }
    } else if (shape_ptr->kind == WGR_SHAPE3D_SPHERE) {
        hit = wgri_pick_ray_sphere(local, (vec3_t){0, 0, 0}, shape_ptr->dim[0], &h);
    } else {
        return false; /* lines have no area to hit */
    }

    if (hit) {
        wgri_pick_result_from_local(&h, world, model, out);
    } else {
        *out = (wgr_pick_result_t){0};
    }
    return true;
}
