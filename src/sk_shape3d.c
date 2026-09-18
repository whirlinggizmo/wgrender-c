#include "sk_shape3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "sk_color.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_pick.h"
#include "internal/sk_scene.h"
#include "sk_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define SK_CIRCLE_SEGMENTS 36

#define MAX_SHAPES3D 1024
#define MAX_STRIP_POINTS 65536

typedef enum {
    SK_SHAPE3D_NONE = 0,
    SK_SHAPE3D_CUBE = 1,
    SK_SHAPE3D_SPHERE = 2,
    SK_SHAPE3D_RECTANGLE = 3,  /* filled, XY plane */
    SK_SHAPE3D_CIRCLE = 4,     /* outline, XY plane */
    SK_SHAPE3D_LINE = 5,
    SK_SHAPE3D_LINE_STRIP = 6,
} sk_shape3d_kind_t;

typedef struct {
    sk_shape3d_kind_t kind;
    float dim[6];   /* cube: w,h,l; sphere/circle: radius; rectangle: w,h; line: x0,y0,z0,x1,y1,z1 */
    vec3_t *points; /* line strip */
    int point_count;
    int point_capacity;
    vec3_t position;
    vec3_t rotation; /* radians */
    vec3_t scale;
    sk_color_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} sk_shape3d_t;

static sk_shape3d_t sk_shapes3d[MAX_SHAPES3D];
static sk_handle_pool_t sk_shape3d_pool;
static uint16_t sk_shape3d_free_indices[MAX_SHAPES3D];
static uint16_t sk_shape3d_generations[MAX_SHAPES3D];
static unsigned char sk_shape3d_occupied[MAX_SHAPES3D];

static void draw_handle(sk_handle_t shape);
static void draw_opaque(sk_handle_t shape);
static int collect_transparent(sk_handle_t shape, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items);
static void draw_transparent(sk_handle_t shape, int part);
static bool shape_bounds(sk_handle_t shape, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model);
static bool shape_pick(sk_handle_t shape, vec3_t origin, vec3_t dir, sk_pick_result_t *out);

void sk_shape3d_init(void)
{
    memset(sk_shapes3d, 0, sizeof(sk_shapes3d));
    sk_handle_pool_init(&sk_shape3d_pool,
                        SK_HANDLE_KIND_SHAPE3D,
                        MAX_SHAPES3D,
                        sk_shape3d_free_indices,
                        MAX_SHAPES3D,
                        sk_shape3d_generations,
                        sk_shape3d_occupied);
    sk_scene_register_passes(SK_HANDLE_KIND_SHAPE3D, draw_opaque, collect_transparent, draw_transparent);
    sk_scene_register_bounds(SK_HANDLE_KIND_SHAPE3D, shape_bounds);
    sk_scene_register_pick(SK_HANDLE_KIND_SHAPE3D, shape_pick);
    sk_scene_register_enabled(SK_HANDLE_KIND_SHAPE3D, sk_shape3d_is_enabled);
}

void sk_shape3d_deinit(void)
{
    for (int i = 0; i < MAX_SHAPES3D; i++) {
        free(sk_shapes3d[i].points);
    }
    memset(sk_shapes3d, 0, sizeof(sk_shapes3d));
    sk_handle_pool_reset(&sk_shape3d_pool);
}

static void set_color(sk_color_t color)
{
    sk_colorf_t c = sk_color_unpack(color);
    sgl_c4f(c.r, c.g, c.b, c.a);
}

SK_KEEP
void sk_shape3d_draw_line(float x0, float y0, float z0,
                           float x1, float y1, float z1, sk_color_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v3f(x0, y0, z0);
    sgl_v3f(x1, y1, z1);
    sgl_end();
}

SK_KEEP
void sk_shape3d_draw_cube(float cx, float cy, float cz,
                        float width, float height, float length, sk_color_t color)
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

SK_KEEP
void sk_shape3d_draw_cube_wires(float cx, float cy, float cz,
                              float width, float height, float length, sk_color_t color)
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

SK_KEEP
void sk_shape3d_draw_sphere(float cx, float cy, float cz, float radius, sk_color_t color)
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

static void rectangle_xy(float width, float height, sk_color_t color)
{
    const float hw = width * 0.5f, hh = height * 0.5f;
    sgl_begin_quads();
    set_color(color);
    sgl_v3f(-hw, -hh, 0.0f); sgl_v3f(hw, -hh, 0.0f); sgl_v3f(hw, hh, 0.0f); sgl_v3f(-hw, hh, 0.0f);
    sgl_end();
}

static void circle_xy(float radius, sk_color_t color)
{
    sgl_begin_line_strip();
    set_color(color);
    for (int i = 0; i <= SK_CIRCLE_SEGMENTS; i++) {
        const float a = (float)(2.0 * M_PI * i / SK_CIRCLE_SEGMENTS);
        sgl_v3f(cosf(a) * radius, sinf(a) * radius, 0.0f);
    }
    sgl_end();
}

SK_KEEP
void sk_shape3d_draw_rectangle(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, sk_color_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    rectangle_xy(width, height, color);
    sgl_pop_matrix();
}

SK_KEEP
void sk_shape3d_draw_circle(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, sk_color_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    circle_xy(radius, color);
    sgl_pop_matrix();
}

SK_KEEP
void sk_shape3d_draw_grid(int slices, float spacing, sk_color_t color)
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

static sk_shape3d_t *resolve(sk_handle_t shape)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_shape3d_pool, shape, &index)) {
        if (shape != 0) {
            log_warn("Invalid shape handle (%u)", (unsigned int)shape);
        }
        return NULL;
    }
    return &sk_shapes3d[index];
}

SK_KEEP
sk_handle_t sk_shape3d_create(void)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_shape3d_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SHAPES3D reached (%d)", MAX_SHAPES3D);
        return 0;
    }
    sk_handle_pool_resolve(&sk_shape3d_pool, handle, &index);
    sk_shapes3d[index] = (sk_shape3d_t){
        .kind = SK_SHAPE3D_NONE,
        .dim = {1.0f, 1.0f, 1.0f},
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .color = SK_COLOR_WHITE,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

SK_KEEP
void sk_shape3d_destroy(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return;
    }
    free(shape_ptr->points);
    *shape_ptr = (sk_shape3d_t){0};
    sk_handle_pool_free(&sk_shape3d_pool, shape);
}

SK_KEEP
bool sk_shape3d_set_cube(sk_handle_t shape, float width, float height, float length)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_CUBE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    shape_ptr->dim[2] = length;
    return true;
}

SK_KEEP
bool sk_shape3d_set_sphere(sk_handle_t shape, float radius)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_SPHERE;
    shape_ptr->dim[0] = radius;
    return true;
}

SK_KEEP
bool sk_shape3d_set_rectangle(sk_handle_t shape, float width, float height)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_RECTANGLE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    return true;
}

SK_KEEP
bool sk_shape3d_set_circle(sk_handle_t shape, float radius)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_CIRCLE;
    shape_ptr->dim[0] = radius;
    return true;
}

SK_KEEP
bool sk_shape3d_set_line(sk_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_LINE;
    shape_ptr->dim[0] = x0; shape_ptr->dim[1] = y0; shape_ptr->dim[2] = z0;
    shape_ptr->dim[3] = x1; shape_ptr->dim[4] = y1; shape_ptr->dim[5] = z1;
    return true;
}

SK_KEEP
bool sk_shape3d_set_line_strip(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE3D_LINE_STRIP;
    shape_ptr->point_count = 0; /* keeps the allocation for rebuilding */
    return true;
}

SK_KEEP
bool sk_shape3d_add_point(sk_handle_t shape, float x, float y, float z)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || shape_ptr->kind != SK_SHAPE3D_LINE_STRIP) {
        if (shape_ptr != NULL) log_warn("sk_shape3d_add_point: shape isn't a line strip (sk_shape3d_set_line_strip)");
        return false;
    }
    if (shape_ptr->point_count >= MAX_STRIP_POINTS) {
        log_warn("sk_shape3d_add_point: line strip is full (%d points)", MAX_STRIP_POINTS);
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

SK_KEEP
int sk_shape3d_get_point_count(sk_handle_t shape)
{
    const sk_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->kind == SK_SHAPE3D_LINE_STRIP ? shape_ptr->point_count : 0;
}

SK_KEEP
bool sk_shape3d_set_transform(sk_handle_t shape,
                              float position_x, float position_y, float position_z,
                              float rotation_x, float rotation_y, float rotation_z,
                              float scale_x, float scale_y, float scale_z)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->position = (vec3_t){position_x, position_y, position_z};
    shape_ptr->rotation = (vec3_t){rotation_x, rotation_y, rotation_z};
    shape_ptr->scale = (vec3_t){scale_x, scale_y, scale_z};
    return true;
}

SK_KEEP
bool sk_shape3d_set_color(sk_handle_t shape, sk_color_t color)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->color = color;
    return true;
}

SK_KEEP
bool sk_shape3d_set_visible(sk_handle_t shape, bool visible)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_shape3d_is_visible(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->visible;
}

SK_KEEP
bool sk_shape3d_set_pickable(sk_handle_t shape, bool pickable)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_shape3d_is_pickable(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->pickable;
}

SK_KEEP
bool sk_shape3d_set_enabled(sk_handle_t shape, bool enabled)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_shape3d_is_enabled(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->enabled;
}

static void draw_handle(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE3D_NONE) {
        return;
    }
    sgl_push_matrix();
    sgl_translate(shape_ptr->position.x, shape_ptr->position.y, shape_ptr->position.z);
    sgl_rotate(shape_ptr->rotation.z, 0.0f, 0.0f, 1.0f);
    sgl_rotate(shape_ptr->rotation.y, 0.0f, 1.0f, 0.0f);
    sgl_rotate(shape_ptr->rotation.x, 1.0f, 0.0f, 0.0f);
    sgl_scale(shape_ptr->scale.x, shape_ptr->scale.y, shape_ptr->scale.z);

    switch (shape_ptr->kind) {
        case SK_SHAPE3D_CUBE:
            sk_shape3d_draw_cube(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2], shape_ptr->color);
            break;
        case SK_SHAPE3D_SPHERE:
            sk_shape3d_draw_sphere(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->color);
            break;
        case SK_SHAPE3D_RECTANGLE:
            rectangle_xy(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->color);
            break;
        case SK_SHAPE3D_CIRCLE:
            circle_xy(shape_ptr->dim[0], shape_ptr->color);
            break;
        case SK_SHAPE3D_LINE:
            sk_shape3d_draw_line(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2],
                                  shape_ptr->dim[3], shape_ptr->dim[4], shape_ptr->dim[5], shape_ptr->color);
            break;
        case SK_SHAPE3D_LINE_STRIP:
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
static bool is_translucent(sk_handle_t shape)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && sk_color_unpack(shape_ptr->color).a < 1.0f;
}

static void draw_opaque(sk_handle_t shape)
{
    if (!is_translucent(shape)) {
        draw_handle(shape);
    }
}

static int collect_transparent(sk_handle_t shape, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE3D_NONE ||
        !is_translucent(shape) || max_items < 1) {
        return 0;
    }
    out[0] = (sk_transparent_item_t){
        .handle = shape,
        .part = 0,
        .depth = sk_scene_view_depth(cam, shape_ptr->position),
    };
    return 1;
}

static void draw_transparent(sk_handle_t shape, int part)
{
    (void)part;
    draw_handle(shape);
}

SK_KEEP
void sk_shape3d_draw(sk_handle_t shape)
{
    draw_handle(shape);
}

static bool shape_bounds(sk_handle_t shape, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    float hx, hy, hz;
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE3D_NONE) {
        return false;
    }
    switch (shape_ptr->kind) {
        case SK_SHAPE3D_CUBE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = shape_ptr->dim[2] * 0.5f;
            break;
        case SK_SHAPE3D_RECTANGLE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = 0.0f;
            break;
        case SK_SHAPE3D_CIRCLE:
            hx = hy = shape_ptr->dim[0]; hz = 0.0f;
            break;
        case SK_SHAPE3D_LINE:
        case SK_SHAPE3D_LINE_STRIP: {
            const int count = shape_ptr->kind == SK_SHAPE3D_LINE ? 2 : shape_ptr->point_count;
            if (count == 0) return false;
            *lmin = (vec3_t){1e30f, 1e30f, 1e30f};
            *lmax = (vec3_t){-1e30f, -1e30f, -1e30f};
            for (int i = 0; i < count; i++) {
                const vec3_t p = shape_ptr->kind == SK_SHAPE3D_LINE
                                     ? (vec3_t){shape_ptr->dim[i * 3], shape_ptr->dim[i * 3 + 1], shape_ptr->dim[i * 3 + 2]}
                                     : shape_ptr->points[i];
                lmin->x = fminf(lmin->x, p.x); lmin->y = fminf(lmin->y, p.y); lmin->z = fminf(lmin->z, p.z);
                lmax->x = fmaxf(lmax->x, p.x); lmax->y = fmaxf(lmax->y, p.y); lmax->z = fmaxf(lmax->z, p.z);
            }
            *model = sk_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
            return true;
        }
        default: /* sphere: dim[0] = radius */
            hx = hy = hz = shape_ptr->dim[0];
            break;
    }
    *lmin = (vec3_t){-hx, -hy, -hz};
    *lmax = (vec3_t){hx, hy, hz};
    *model = sk_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
    return true;
}

static bool shape_pick(sk_handle_t shape, vec3_t origin, vec3_t dir, sk_pick_result_t *out)
{
    sk_shape3d_t *shape_ptr = resolve(shape);
    sk_mat4_t model;
    sk_ray_t world, local;
    sk_ray_hit_t h = {0};
    vec3_t lmin, lmax;
    bool hit;

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable || shape_ptr->kind == SK_SHAPE3D_NONE) {
        return false;
    }

    model = sk_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
    world.origin = origin;
    world.dir = dir;
    local = sk_pick_ray_to_local(model, world);

    if (shape_ptr->kind == SK_SHAPE3D_CUBE) {
        float hx = shape_ptr->dim[0] * 0.5f;
        float hy = shape_ptr->dim[1] * 0.5f;
        float hz = shape_ptr->dim[2] * 0.5f;
        lmin = (vec3_t){-hx, -hy, -hz};
        lmax = (vec3_t){hx, hy, hz};
        hit = sk_pick_ray_aabb(local, lmin, lmax, &h);
    } else if (shape_ptr->kind == SK_SHAPE3D_RECTANGLE || shape_ptr->kind == SK_SHAPE3D_CIRCLE) {
        /* the XY plane (both sides); circles are picked anywhere inside the outline */
        hit = false;
        if (fabsf(local.dir.z) > 1e-6f) { /* parallel (edge-on): no area to hit */
            const float t = -local.origin.z / local.dir.z;
            const float px = local.origin.x + local.dir.x * t, py = local.origin.y + local.dir.y * t;
            const bool inside = shape_ptr->kind == SK_SHAPE3D_RECTANGLE
                                    ? fabsf(px) <= shape_ptr->dim[0] * 0.5f && fabsf(py) <= shape_ptr->dim[1] * 0.5f
                                    : px * px + py * py <= shape_ptr->dim[0] * shape_ptr->dim[0];
            if (t >= 0.0f && inside) {
                h = (sk_ray_hit_t){.hit = true, .t = t, .point = {px, py, 0.0f},
                                   .normal = {0.0f, 0.0f, local.dir.z < 0.0f ? 1.0f : -1.0f}};
                hit = true;
            }
        }
    } else if (shape_ptr->kind == SK_SHAPE3D_SPHERE) {
        hit = sk_pick_ray_sphere(local, (vec3_t){0, 0, 0}, shape_ptr->dim[0], &h);
    } else {
        return false; /* lines have no area to hit */
    }

    if (hit) {
        sk_pick_result_from_local(&h, world, model, out);
    } else {
        *out = (sk_pick_result_t){0};
    }
    return true;
}
