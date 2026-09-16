#include "sk_shape.h"

#include <math.h>
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

typedef enum {
    SK_SHAPE_NONE = 0,
    SK_SHAPE_CUBE = 1,
    SK_SHAPE_SPHERE = 2,
} sk_shape_kind_t;

typedef struct {
    sk_shape_kind_t kind;
    float dim[3];   /* cube: w,h,l   sphere: radius in dim[0] */
    vec3_t position;
    vec3_t rotation; /* radians */
    vec3_t scale;
    sk_handle_t color;
    bool visible;
    bool pickable;
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
}

void sk_shape_deinit(void)
{
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

static void draw_handle(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE) {
        return;
    }

    sgl_push_matrix();
    sgl_translate(shape_ptr->position.x, shape_ptr->position.y, shape_ptr->position.z);
    sgl_rotate(shape_ptr->rotation.z, 0.0f, 0.0f, 1.0f);
    sgl_rotate(shape_ptr->rotation.y, 0.0f, 1.0f, 0.0f);
    sgl_rotate(shape_ptr->rotation.x, 1.0f, 0.0f, 0.0f);
    sgl_scale(shape_ptr->scale.x, shape_ptr->scale.y, shape_ptr->scale.z);

    if (shape_ptr->kind == SK_SHAPE_CUBE) {
        sk_shape_draw_cube(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->dim[1], shape_ptr->dim[2], shape_ptr->color);
    } else if (shape_ptr->kind == SK_SHAPE_SPHERE) {
        sk_shape_draw_sphere(0.0f, 0.0f, 0.0f, shape_ptr->dim[0], shape_ptr->color);
    }

    sgl_pop_matrix();
}

/* Scene passes: a shape is opaque unless its color is translucent. */
static bool is_translucent(sk_handle_t shape)
{
    sk_shape_t *shape_ptr = resolve(shape);
    return shape_ptr != NULL && sk_color_get(shape_ptr->color).a < 1.0f;
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
    sk_shape_t *shape_ptr = resolve(shape);
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE ||
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
void sk_shape_draw(sk_handle_t shape)
{
    draw_handle(shape);
}

static bool shape_bounds(sk_handle_t shape, vec3_t *lmin, vec3_t *lmax, sk_mat4_t *model)
{
    sk_shape_t *shape_ptr = resolve(shape);
    float hx, hy, hz;
    if (shape_ptr == NULL || !shape_ptr->visible || shape_ptr->kind == SK_SHAPE_NONE) {
        return false;
    }
    if (shape_ptr->kind == SK_SHAPE_CUBE) {
        hx = shape_ptr->dim[0] * 0.5f; hy = shape_ptr->dim[1] * 0.5f; hz = shape_ptr->dim[2] * 0.5f;
    } else { /* sphere: dim[0] = radius */
        hx = hy = hz = shape_ptr->dim[0];
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

    if (out == NULL || shape_ptr == NULL || !shape_ptr->visible || !shape_ptr->pickable || shape_ptr->kind == SK_SHAPE_NONE) {
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
    } else {
        hit = sk_pick_ray_sphere(local, (vec3_t){0, 0, 0}, shape_ptr->dim[0], &h);
    }

    if (hit) {
        sk_pick_result_from_local(&h, world, model, out);
    } else {
        *out = (sk_pick_result_t){0};
    }
    return true;
}
