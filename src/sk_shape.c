#include "sk_shape.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_internal.h"
#include "internal/sk_math.h"
#include "internal/sk_pick.h"
#include "internal/sk_scene.h"
#include "sk_logger.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define SK_CIRCLE_SEGMENTS 36

#define MAX_SHAPES 1024
#define MAX_STRIP_POINTS 65536

typedef enum {
    SK_SHAPE_NONE = 0,
    SK_SHAPE_CUBE = 1,
    SK_SHAPE_SPHERE = 2,
    SK_SHAPE_RECTANGLE = 3,  /* filled, XY plane */
    SK_SHAPE_CIRCLE = 4,     /* outline, XY plane */
    SK_SHAPE_LINE = 5,
    SK_SHAPE_LINE_STRIP = 6,
    /* 2D (screen space): scene 2D members, placed by the 2D transform */
    SK_SHAPE_RECTANGLE_2D = 7, /* dim: width, height, corner radius; origin at the top-left */
    SK_SHAPE_CIRCLE_2D = 8,    /* dim: radius; origin at the center */
    SK_SHAPE_LINE_2D = 9,      /* dim: x0, y0, x1, y1, thickness */
} sk_shape_kind_t;

#define SK_SHAPE_CORNER_SEGMENTS 8
#define SHAPE_PI ((float)M_PI)

static bool is_2d_kind(sk_shape_kind_t kind)
{
    return kind == SK_SHAPE_RECTANGLE_2D || kind == SK_SHAPE_CIRCLE_2D || kind == SK_SHAPE_LINE_2D;
}

typedef struct {
    sk_shape_kind_t kind;
    float dim[6];   /* cube: w,h,l; sphere/circle: radius; rectangle: w,h; line: x0,y0,z0,x1,y1,z1 */
    vec3_t *points; /* line strip */
    int point_count;
    int point_capacity;
    vec3_t position; /* 2D: x, y */
    vec3_t rotation; /* radians; 2D: z */
    vec3_t scale;    /* 2D: x, y */
    float outline;   /* 2D rectangles and circles: stroke thickness; 0 = filled */
    sk_handle_t color;
    bool visible;
    bool pickable;
    bool enabled;  /* false: hits block the pointer but don't react (scene interaction) */
} sk_shape_t;

static sk_shape_t sk_shapes[MAX_SHAPES];
static sk_handle_pool_t sk_shape_pool;
static uint16_t sk_shape_free_indices[MAX_SHAPES];
static uint16_t sk_shape_generations[MAX_SHAPES];
static unsigned char sk_shape_occupied[MAX_SHAPES];

static void draw_handle(sk_handle_t shape);
static void draw_opaque(sk_handle_t shape);
static int collect_transparent(sk_handle_t shape, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items);
static void draw_transparent(sk_handle_t shape, int part);
static bool shape_bounds(sk_handle_t shape, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model);
static bool shape_pick(sk_handle_t shape, vec3_t origin, vec3_t dir, sk_pick_result_t *out);
static void draw_2d(sk_handle_t shape);
static bool is_2d(sk_handle_t shape);
static bool pick_2d(sk_handle_t shape, float screen_x, float screen_y, sk_pick_result_t *out);

void sk_shape_init(void)
{
    memset(sk_shapes, 0, sizeof(sk_shapes));
    sk_handle_pool_init(&sk_shape_pool,
                        SK_HANDLE_KIND_SHAPE,
                        MAX_SHAPES,
                        sk_shape_free_indices,
                        MAX_SHAPES,
                        sk_shape_generations,
                        sk_shape_occupied);
    sk_scene_register_passes(SK_HANDLE_KIND_SHAPE, draw_opaque, collect_transparent, draw_transparent);
    sk_scene_register_bounds(SK_HANDLE_KIND_SHAPE, shape_bounds);
    sk_scene_register_pick(SK_HANDLE_KIND_SHAPE, shape_pick);
    sk_scene_register_2d(SK_HANDLE_KIND_SHAPE, draw_2d, pick_2d);
    sk_scene_register_is_2d(SK_HANDLE_KIND_SHAPE, is_2d);
    sk_scene_register_enabled(SK_HANDLE_KIND_SHAPE, sk_shape_is_enabled);
}

void sk_shape_deinit(void)
{
    for (int i = 0; i < MAX_SHAPES; i++) {
        free(sk_shapes[i].points);
    }
    memset(sk_shapes, 0, sizeof(sk_shapes));
    sk_handle_pool_reset(&sk_shape_pool);
}

static void set_color(sk_handle_t color)
{
    color_t c = sk_color_get(color);
    sgl_c4f(c.r, c.g, c.b, c.a);
}

SK_KEEP
void sk_shape_draw_rectangle(int x, int y, int width, int height, sk_handle_t color)
{
    const float x0 = (float)x;
    const float y0 = (float)y;
    const float x1 = (float)(x + width);
    const float y1 = (float)(y + height);

    sgl_begin_quads();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x0, y1);
    sgl_end();
}

SK_KEEP
void sk_shape_draw_rectangle_lines(int x, int y, int width, int height, sk_handle_t color)
{
    const float x0 = (float)x;
    const float y0 = (float)y;
    const float x1 = (float)(x + width);
    const float y1 = (float)(y + height);

    sgl_begin_line_strip();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x0, y1);
    sgl_v2f(x0, y0);
    sgl_end();
}

SK_KEEP
void sk_shape_draw_line(int start_x, int start_y, int end_x, int end_y, sk_handle_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v2f((float)start_x, (float)start_y);
    sgl_v2f((float)end_x, (float)end_y);
    sgl_end();
}

SK_KEEP
void sk_shape_draw_circle(int center_x, int center_y, float radius, sk_handle_t color)
{
    const float cx = (float)center_x;
    const float cy = (float)center_y;

    sgl_begin_triangles();
    set_color(color);
    for (int i = 0; i < SK_CIRCLE_SEGMENTS; i++) {
        float a0 = (float)(2.0 * M_PI * i / SK_CIRCLE_SEGMENTS);
        float a1 = (float)(2.0 * M_PI * (i + 1) / SK_CIRCLE_SEGMENTS);
        sgl_v2f(cx, cy);
        sgl_v2f(cx + cosf(a0) * radius, cy + sinf(a0) * radius);
        sgl_v2f(cx + cosf(a1) * radius, cy + sinf(a1) * radius);
    }
    sgl_end();
}

SK_KEEP
void sk_shape_draw_circle_lines(int center_x, int center_y, float radius, sk_handle_t color)
{
    const float cx = (float)center_x;
    const float cy = (float)center_y;

    sgl_begin_line_strip();
    set_color(color);
    for (int i = 0; i <= SK_CIRCLE_SEGMENTS; i++) {
        float a = (float)(2.0 * M_PI * i / SK_CIRCLE_SEGMENTS);
        sgl_v2f(cx + cosf(a) * radius, cy + sinf(a) * radius);
    }
    sgl_end();
}

SK_KEEP
void sk_shape_draw_triangle(float x0, float y0, float x1, float y1, float x2, float y2,
                            sk_handle_t color)
{
    sgl_begin_triangles();
    set_color(color);
    sgl_v2f(x0, y0);
    sgl_v2f(x1, y1);
    sgl_v2f(x2, y2);
    sgl_end();
}

/* ------------------------------------------------------------------ 3D ----- */

SK_KEEP
void sk_shape_draw_line_3d(float x0, float y0, float z0,
                           float x1, float y1, float z1, sk_handle_t color)
{
    sgl_begin_lines();
    set_color(color);
    sgl_v3f(x0, y0, z0);
    sgl_v3f(x1, y1, z1);
    sgl_end();
}

SK_KEEP
void sk_shape_draw_cube(float cx, float cy, float cz,
                        float width, float height, float length, sk_handle_t color)
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
void sk_shape_draw_cube_wires(float cx, float cy, float cz,
                              float width, float height, float length, sk_handle_t color)
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
void sk_shape_draw_sphere(float cx, float cy, float cz, float radius, sk_handle_t color)
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

static void rectangle_xy(float width, float height, sk_handle_t color)
{
    const float hw = width * 0.5f, hh = height * 0.5f;
    sgl_begin_quads();
    set_color(color);
    sgl_v3f(-hw, -hh, 0.0f); sgl_v3f(hw, -hh, 0.0f); sgl_v3f(hw, hh, 0.0f); sgl_v3f(-hw, hh, 0.0f);
    sgl_end();
}

static void circle_xy(float radius, sk_handle_t color)
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
void sk_shape_draw_rectangle_3d(float cx, float cy, float cz, float width, float height,
                                float rx, float ry, float rz, sk_handle_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    rectangle_xy(width, height, color);
    sgl_pop_matrix();
}

SK_KEEP
void sk_shape_draw_circle_3d(float cx, float cy, float cz, float radius,
                             float rx, float ry, float rz, sk_handle_t color)
{
    push_placement(cx, cy, cz, rx, ry, rz);
    circle_xy(radius, color);
    sgl_pop_matrix();
}

SK_KEEP
void sk_shape_draw_grid(int slices, float spacing, sk_handle_t color)
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

static sk_shape_t *resolve(sk_handle_t shape)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_shape_pool, shape, &index)) {
        if (shape != 0) {
            log_warn("Invalid shape handle (%u)", (unsigned int)shape);
        }
        return NULL;
    }
    return &sk_shapes[index];
}

SK_KEEP
sk_handle_t sk_shape_create(void)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_shape_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SHAPES reached (%d)", MAX_SHAPES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_shape_pool, handle, &index);
    sk_shapes[index] = (sk_shape_t){
        .kind = SK_SHAPE_NONE,
        .dim = {1.0f, 1.0f, 1.0f},
        .position = {0.0f, 0.0f, 0.0f},
        .rotation = {0.0f, 0.0f, 0.0f},
        .scale = {1.0f, 1.0f, 1.0f},
        .color = 0,
        .visible = true,
        .pickable = true,
        .enabled = true,
    };
    return handle;
}

SK_KEEP
void sk_shape_destroy(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return;
    }
    free(shape_ptr->points);
    *shape_ptr = (sk_shape_t){0};
    sk_handle_pool_free(&sk_shape_pool, shape);
}

SK_KEEP
bool sk_shape_set_cube(sk_handle_t shape, float width, float height, float length)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_CUBE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    shape_ptr->dim[2] = length;
    return true;
}

SK_KEEP
bool sk_shape_set_sphere(sk_handle_t shape, float radius)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_SPHERE;
    shape_ptr->dim[0] = radius;
    return true;
}

SK_KEEP
bool sk_shape_set_rectangle(sk_handle_t shape, float width, float height)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_RECTANGLE;
    shape_ptr->dim[0] = width;
    shape_ptr->dim[1] = height;
    return true;
}

SK_KEEP
bool sk_shape_set_circle(sk_handle_t shape, float radius)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_CIRCLE;
    shape_ptr->dim[0] = radius;
    return true;
}

SK_KEEP
bool sk_shape_set_line(sk_handle_t shape, float x0, float y0, float z0, float x1, float y1, float z1)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_LINE;
    shape_ptr->dim[0] = x0; shape_ptr->dim[1] = y0; shape_ptr->dim[2] = z0;
    shape_ptr->dim[3] = x1; shape_ptr->dim[4] = y1; shape_ptr->dim[5] = z1;
    return true;
}

SK_KEEP
bool sk_shape_set_line_strip(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = SK_SHAPE_LINE_STRIP;
    shape_ptr->point_count = 0; /* keeps the allocation for rebuilding */
    return true;
}

SK_KEEP
bool sk_shape_add_point(sk_handle_t shape, float x, float y, float z)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || shape_ptr->kind != SK_SHAPE_LINE_STRIP) {
        if (shape_ptr != NULL) log_warn("sk_shape_add_point: shape isn't a line strip (sk_shape_set_line_strip)");
        return false;
    }
    if (shape_ptr->point_count >= MAX_STRIP_POINTS) {
        log_warn("sk_shape_add_point: line strip is full (%d points)", MAX_STRIP_POINTS);
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
int sk_shape_get_point_count(sk_handle_t shape)
{
    const sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->kind == SK_SHAPE_LINE_STRIP ? shape_ptr->point_count : 0;
}

/* One shape kind with dim[] set; resets strip points. */
static bool set_kind(sk_handle_t shape, sk_shape_kind_t kind, const float *dim, int count)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->kind = kind;
    memset(shape_ptr->dim, 0, sizeof(shape_ptr->dim));
    memcpy(shape_ptr->dim, dim, (size_t)count * sizeof(float));
    shape_ptr->point_count = 0;
    return true;
}

SK_KEEP
bool sk_shape_set_rectangle_2d(sk_handle_t shape, float width, float height, float corner_radius)
{
    const float radius = fmaxf(0.0f, fminf(corner_radius, fminf(width, height) * 0.5f));
    return set_kind(shape, SK_SHAPE_RECTANGLE_2D, (float[]){width, height, radius}, 3);
}

SK_KEEP
bool sk_shape_set_circle_2d(sk_handle_t shape, float radius)
{
    return set_kind(shape, SK_SHAPE_CIRCLE_2D, (float[]){radius}, 1);
}

SK_KEEP
bool sk_shape_set_line_2d(sk_handle_t shape, float x0, float y0, float x1, float y1, float thickness)
{
    return set_kind(shape, SK_SHAPE_LINE_2D, (float[]){x0, y0, x1, y1, thickness > 0.0f ? thickness : 1.0f}, 5);
}

SK_KEEP
bool sk_shape_set_transform_2d(sk_handle_t shape, float x, float y, float rotation, float scale_x, float scale_y)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->position = (vec3_t){x, y, 0.0f};
    shape_ptr->rotation = (vec3_t){0.0f, 0.0f, rotation};
    shape_ptr->scale = (vec3_t){scale_x, scale_y, 1.0f};
    return true;
}

SK_KEEP
bool sk_shape_set_outline(sk_handle_t shape, float thickness)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->outline = thickness > 0.0f ? thickness : 0.0f;
    return true;
}

SK_KEEP
bool sk_shape_set_transform(sk_handle_t shape,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z,
                            float scale_x, float scale_y, float scale_z)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->position = (vec3_t){position_x, position_y, position_z};
    shape_ptr->rotation = (vec3_t){rotation_x, rotation_y, rotation_z};
    shape_ptr->scale = (vec3_t){scale_x, scale_y, scale_z};
    return true;
}

SK_KEEP
bool sk_shape_set_color(sk_handle_t shape, sk_handle_t color)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->color = color;
    return true;
}

SK_KEEP
bool sk_shape_set_visible(sk_handle_t shape, bool visible)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->visible = visible;
    return true;
}

SK_KEEP
bool sk_shape_is_visible(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->visible;
}

SK_KEEP
bool sk_shape_set_pickable(sk_handle_t shape, bool pickable)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->pickable = pickable;
    return true;
}

SK_KEEP
bool sk_shape_is_pickable(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->pickable;
}

SK_KEEP
bool sk_shape_set_enabled(sk_handle_t shape, bool enabled)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL) {
        return false;
    }
    shape_ptr->enabled = enabled;
    return true;
}

SK_KEEP
bool sk_shape_is_enabled(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && shape_ptr->enabled;
}

static void draw_shape_2d(const sk_shape_t *shape_ptr);

static void draw_handle(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE) {
        return;
    }
    if (is_2d_kind(shape_ptr->kind)) {
        draw_shape_2d(shape_ptr);
        return;
    }

    sgl_push_matrix();
    sgl_translate(shape_ptr->position.x, shape_ptr->position.y, shape_ptr->position.z);
    sgl_rotate(shape_ptr->rotation.z, 0.0f, 0.0f, 1.0f);
    sgl_rotate(shape_ptr->rotation.y, 0.0f, 1.0f, 0.0f);
    sgl_rotate(shape_ptr->rotation.x, 1.0f, 0.0f, 0.0f);
    sgl_scale(shape_ptr->scale.x, shape_ptr->scale.y, shape_ptr->scale.z);

    switch (shape_ptr->kind) {
        case SK_SHAPE_CUBE:
            sk_shape_draw_cube(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2], shape_ptr->color);
            break;
        case SK_SHAPE_SPHERE:
            sk_shape_draw_sphere(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->color);
            break;
        case SK_SHAPE_RECTANGLE:
            rectangle_xy(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->color);
            break;
        case SK_SHAPE_CIRCLE:
            circle_xy(shape_ptr->dim[0], shape_ptr->color);
            break;
        case SK_SHAPE_LINE:
            sk_shape_draw_line_3d(shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2],
                                  shape_ptr->dim[3], shape_ptr->dim[4], shape_ptr->dim[5], shape_ptr->color);
            break;
        case SK_SHAPE_LINE_STRIP:
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
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && sk_color_get(shape_ptr->color).a < 1.0f;
}

static bool is_2d(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && is_2d_kind(shape_ptr->kind);
}

static void draw_opaque(sk_handle_t shape)
{
    if (!is_2d(shape) && !is_translucent(shape)) {
        draw_handle(shape);
    }
}

static int collect_transparent(sk_handle_t shape, const sk_camera3d_t *cam,
                               sk_transparent_item_t *out, int max_items)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE ||
        is_2d_kind(shape_ptr->kind) || !is_translucent(shape) || max_items < 1) {
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
void sk_shape_draw(sk_handle_t shape)
{
    draw_handle(shape);
}

static bool shape_bounds(sk_handle_t shape, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_shape_t *shape_ptr = resolve(shape);
    float hx, hy, hz;
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE || is_2d_kind(shape_ptr->kind)) {
        return false;
    }
    switch (shape_ptr->kind) {
        case SK_SHAPE_CUBE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = shape_ptr->dim[2] * 0.5f;
            break;
        case SK_SHAPE_RECTANGLE:
            hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = 0.0f;
            break;
        case SK_SHAPE_CIRCLE:
            hx = hy = shape_ptr->dim[0]; hz = 0.0f;
            break;
        case SK_SHAPE_LINE:
        case SK_SHAPE_LINE_STRIP: {
            const int count = shape_ptr->kind == SK_SHAPE_LINE ? 2 : shape_ptr->point_count;
            if (count == 0) return false;
            *lmin = (vec3_t){1e30f, 1e30f, 1e30f};
            *lmax = (vec3_t){-1e30f, -1e30f, -1e30f};
            for (int i = 0; i < count; i++) {
                const vec3_t p = shape_ptr->kind == SK_SHAPE_LINE
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
    sk_shape_t *shape_ptr = resolve(shape);
    sk_mat4_t model;
    sk_ray_t world, local;
    sk_ray_hit_t h = {0};
    vec3_t lmin, lmax;
    bool hit;

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable || shape_ptr->kind == SK_SHAPE_NONE ||
        is_2d_kind(shape_ptr->kind)) {
        return false;
    }

    model = sk_mat4_trs(shape_ptr->position, shape_ptr->rotation, shape_ptr->scale);
    world.origin = origin;
    world.dir = dir;
    local = sk_pick_ray_to_local(model, world);

    if (shape_ptr->kind == SK_SHAPE_CUBE) {
        float hx = shape_ptr->dim[0] * 0.5f;
        float hy = shape_ptr->dim[1] * 0.5f;
        float hz = shape_ptr->dim[2] * 0.5f;
        lmin = (vec3_t){-hx, -hy, -hz};
        lmax = (vec3_t){hx, hy, hz};
        hit = sk_pick_ray_aabb(local, lmin, lmax, &h);
    } else if (shape_ptr->kind == SK_SHAPE_RECTANGLE || shape_ptr->kind == SK_SHAPE_CIRCLE) {
        /* the XY plane (both sides); circles are picked anywhere inside the outline */
        hit = false;
        if (fabsf(local.dir.z) > 1e-6f) { /* parallel (edge-on): no area to hit */
            const float t = -local.origin.z / local.dir.z;
            const float px = local.origin.x + local.dir.x * t, py = local.origin.y + local.dir.y * t;
            const bool inside = shape_ptr->kind == SK_SHAPE_RECTANGLE
                                    ? fabsf(px) <= shape_ptr->dim[0] * 0.5f && fabsf(py) <= shape_ptr->dim[1] * 0.5f
                                    : px * px + py * py <= shape_ptr->dim[0] * shape_ptr->dim[0];
            if (t >= 0.0f && inside) {
                h = (sk_ray_hit_t){.hit = true, .t = t, .point = {px, py, 0.0f},
                                   .normal = {0.0f, 0.0f, local.dir.z < 0.0f ? 1.0f : -1.0f}};
                hit = true;
            }
        }
    } else if (shape_ptr->kind == SK_SHAPE_SPHERE) {
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

/* ------------------------------------------------------------ 2D shapes ---- */

/* The outline of a rounded rectangle inset by `inset` (its radius shrinks with it),
 * clockwise from the top-left corner's arc, SK_SHAPE_CORNER_SEGMENTS + 1 points per
 * corner. */
static int rounded_rect_points(float width, float height, float radius, float inset, float *xy)
{
    const float r = fmaxf(0.0f, radius - inset);
    const float x0 = inset, y0 = inset, x1 = width - inset, y1 = height - inset;
    int n = 0;
    for (int c = 0; c < 4; c++) {
        /* corners: top-left, top-right, bottom-right, bottom-left (y down) */
        const float cx = c == 0 || c == 3 ? x0 + r : x1 - r;
        const float cy = c == 0 || c == 1 ? y0 + r : y1 - r;
        const float start = SHAPE_PI + (float)c * SHAPE_PI * 0.5f; /* 180, 270, 0, 90 degrees */
        for (int i = 0; i <= SK_SHAPE_CORNER_SEGMENTS; i++) {
            const float a = start + (float)i / SK_SHAPE_CORNER_SEGMENTS * SHAPE_PI * 0.5f;
            xy[n * 2] = cx + cosf(a) * r;
            xy[n * 2 + 1] = cy + sinf(a) * r;
            n++;
        }
    }
    return n;
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

static int circle_points(float radius, float *xy)
{
    for (int i = 0; i < SK_CIRCLE_SEGMENTS; i++) {
        const float a = (float)i / SK_CIRCLE_SEGMENTS * 2.0f * SHAPE_PI;
        xy[i * 2] = cosf(a) * radius;
        xy[i * 2 + 1] = sinf(a) * radius;
    }
    return SK_CIRCLE_SEGMENTS;
}

static void draw_shape_2d(const sk_shape_t *shape_ptr)
{
    float outer[4 * (SK_SHAPE_CORNER_SEGMENTS + 1) * 2 + SK_CIRCLE_SEGMENTS * 2];
    float inner[sizeof(outer) / sizeof(outer[0])];
    const float w = shape_ptr->dim[0], h = shape_ptr->dim[1];
    int n;

    sgl_push_matrix();
    sgl_translate(shape_ptr->position.x, shape_ptr->position.y, 0.0f);
    sgl_rotate(shape_ptr->rotation.z, 0.0f, 0.0f, 1.0f);
    sgl_scale(shape_ptr->scale.x, shape_ptr->scale.y, 1.0f);
    set_color(shape_ptr->color);
    switch (shape_ptr->kind) {
        case SK_SHAPE_RECTANGLE_2D:
            n = rounded_rect_points(w, h, shape_ptr->dim[2], 0.0f, outer);
            if (shape_ptr->outline > 0.0f) {
                rounded_rect_points(w, h, shape_ptr->dim[2], fminf(shape_ptr->outline, fminf(w, h) * 0.5f), inner);
                fill_band(outer, inner, n);
            } else {
                fill_fan(w * 0.5f, h * 0.5f, outer, n);
            }
            break;
        case SK_SHAPE_CIRCLE_2D:
            n = circle_points(shape_ptr->dim[0], outer);
            if (shape_ptr->outline > 0.0f) {
                circle_points(fmaxf(0.0f, shape_ptr->dim[0] - shape_ptr->outline), inner);
                fill_band(outer, inner, n);
            } else {
                fill_fan(0.0f, 0.0f, outer, n);
            }
            break;
        case SK_SHAPE_LINE_2D: {
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

static void draw_2d(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr != NULL && shape_ptr->visible && is_2d_kind(shape_ptr->kind)) {
        draw_shape_2d(shape_ptr);
    }
}

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

static bool pick_2d(sk_handle_t shape, float screen_x, float screen_y, sk_pick_result_t *out)
{
    sk_shape_t *shape_ptr = resolve(shape);
    float px, py, dx, dy, c, s;
    bool hit = false;

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable ||
        !is_2d_kind(shape_ptr->kind) || shape_ptr->scale.x == 0.0f || shape_ptr->scale.y == 0.0f) {
        return false;
    }
    /* screen -> local: undo translation, rotation, then scale */
    dx = screen_x - shape_ptr->position.x;
    dy = screen_y - shape_ptr->position.y;
    c = cosf(-shape_ptr->rotation.z);
    s = sinf(-shape_ptr->rotation.z);
    px = (dx * c - dy * s) / shape_ptr->scale.x;
    py = (dx * s + dy * c) / shape_ptr->scale.y;

    switch (shape_ptr->kind) {
        case SK_SHAPE_RECTANGLE_2D: {
            const float w = shape_ptr->dim[0], h = shape_ptr->dim[1], r = shape_ptr->dim[2];
            hit = inside_rounded_rect(px, py, w, h, r, 0.0f) &&
                  (shape_ptr->outline <= 0.0f ||
                   !inside_rounded_rect(px, py, w, h, r, fminf(shape_ptr->outline, fminf(w, h) * 0.5f)));
            break;
        }
        case SK_SHAPE_CIRCLE_2D: {
            const float d2 = px * px + py * py, r = shape_ptr->dim[0];
            const float inner = shape_ptr->outline > 0.0f ? fmaxf(0.0f, r - shape_ptr->outline) : 0.0f;
            hit = d2 <= r * r && (shape_ptr->outline <= 0.0f || d2 >= inner * inner);
            break;
        }
        case SK_SHAPE_LINE_2D: {
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
    *out = (sk_pick_result_t){
        .hit = true,
        .handle = shape,
        .point_world = {screen_x, screen_y, 0.0f},
        .point_local = {px, py, 0.0f},
        .normal_world = {0.0f, 0.0f, 1.0f},
        .normal_local = {0.0f, 0.0f, 1.0f},
    };
    return true;
}
