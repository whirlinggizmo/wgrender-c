#include "rl_sprite3d.h"

#include <raylib.h>
#include <raymath.h>
#include <stdbool.h>
#include <math.h>

#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_color.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_render_pass.h"
#include "internal/rl_sprite3d.h"
#include "rl_texture.h"
#include "internal/rl_scene.h"
#include "internal/rl_texture.h"
#include <rlgl.h>

#define MAX_SPRITE3D 1024

typedef struct
{
    bool in_use;
    rl_handle_t texture;
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float scale_x;
    float scale_y;
    float scale_z;
    float size;
    rl_handle_t tint_handle;
    bool visible;
    bool pickable;
    rl_sprite3d_facing_t facing;
} rl_sprite3d_instance_t;

static rl_sprite3d_instance_t rl_sprite3d[MAX_SPRITE3D];
static rl_handle_pool_t rl_sprite3d_pool;
static uint16_t rl_sprite3d_free_indices[MAX_SPRITE3D];
static uint16_t rl_sprite3d_generations[MAX_SPRITE3D];
static unsigned char rl_sprite3d_occupied[MAX_SPRITE3D];

static rl_sprite3d_instance_t *resolve_sprite3d_instance(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_sprite3d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid sprite3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_sprite3d[index].in_use) {
        log_warn("Stale sprite3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_sprite3d[index];
}

static float sprite3d_texture_aspect(const Texture2D *texture)
{
    if (texture == NULL || texture->height == 0) {
        return 1.0f;
    }
    return fabsf((float)texture->width / (float)texture->height);
}

static Vector2 sprite3d_billboard_size(const rl_sprite3d_instance_t *sprite, const Texture2D *texture)
{
    float aspect = sprite3d_texture_aspect(texture);
    return (Vector2){
        aspect * sprite->scale_x * sprite->size,
        sprite->scale_y * sprite->size
    };
}

static float sprite3d_y_up_width(const rl_sprite3d_instance_t *sprite, const Texture2D *texture)
{
    return sprite3d_texture_aspect(texture) * sprite->scale_x * sprite->size;
}

static float sprite3d_y_up_length(const rl_sprite3d_instance_t *sprite)
{
    return sprite->scale_z * sprite->size;
}

static Matrix sprite3d_rotation_matrix(const rl_sprite3d_instance_t *sprite)
{
    return MatrixRotateXYZ((Vector3){
        sprite->rotation_x,
        sprite->rotation_y,
        sprite->rotation_z
    });
}

static void draw_textured_quad(const Texture2D *texture, const Vector3 points[4], Color tint)
{
    rlSetTexture(texture->id);
    rlBegin(RL_QUADS);
    rlColor4ub(tint.r, tint.g, tint.b, tint.a);
    /* points[0..3] run bottom-left, bottom-right, top-right, top-left (world +Y up).
     * raylib textures sample v=0 at the top row, so the world-bottom vertices take
     * v=1 and the world-top vertices take v=0 (matches DrawBillboardPro). */
    rlTexCoord2f(0, 1); rlVertex3f(points[0].x, points[0].y, points[0].z);
    rlTexCoord2f(1, 1); rlVertex3f(points[1].x, points[1].y, points[1].z);
    rlTexCoord2f(1, 0); rlVertex3f(points[2].x, points[2].y, points[2].z);
    rlTexCoord2f(0, 0); rlVertex3f(points[3].x, points[3].y, points[3].z);
    rlEnd();
    rlSetTexture(0);
}

static bool build_camera_quad(Camera3D camera,
                              Vector3 position,
                              Vector3 up,
                              Vector2 size,
                              Vector3 points[4],
                              Vector3 *right_axis,
                              Vector3 *up_axis);
static bool build_camera_fixed_y_quad(Camera3D camera,
                                      Vector3 position,
                                      Vector2 size,
                                      Vector3 points[4],
                                      Vector3 *right_axis,
                                      Vector3 *up_axis);
static void build_y_up_quad(Vector3 position,
                            float width,
                            float length,
                            Vector3 points[4],
                            Vector3 *right_axis,
                            Vector3 *forward_axis);
static void build_free_quad(Vector3 position,
                            Matrix rotation,
                            Vector2 size,
                            Vector3 points[4],
                            Vector3 *right_axis,
                            Vector3 *up_axis);

RL_KEEP
rl_handle_t rl_sprite3d_get_default_texture(void)
{
    return rl_texture_get_default();
}

RL_KEEP
rl_handle_t rl_sprite3d_create(rl_handle_t texture)
{
    rl_handle_t handle = 0;
    uint16_t index = 0;

    if (texture != 0 && !rl_texture_retain(texture)) {
        log_warn("Invalid texture handle (%u) for rl_sprite3d_create", texture);
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_sprite3d_pool);
    if (handle == 0)
    {
        log_error("MAX_SPRITE3D reached (%u)", MAX_SPRITE3D);
        if (texture != 0) rl_texture_release(texture);
        return 0;
    }
    rl_handle_pool_resolve(&rl_sprite3d_pool, handle, &index);

    rl_sprite3d[index].texture = texture;
    rl_sprite3d[index].position_x = 0.0f;
    rl_sprite3d[index].position_y = 0.0f;
    rl_sprite3d[index].position_z = 0.0f;
    rl_sprite3d[index].rotation_x = 0.0f;
    rl_sprite3d[index].rotation_y = 0.0f;
    rl_sprite3d[index].rotation_z = 0.0f;
    rl_sprite3d[index].scale_x = 1.0f;
    rl_sprite3d[index].scale_y = 1.0f;
    rl_sprite3d[index].scale_z = 1.0f;
    rl_sprite3d[index].size = 1.0f;
    rl_sprite3d[index].tint_handle = 0;
    rl_sprite3d[index].visible = true;
    rl_sprite3d[index].pickable = true;
    rl_sprite3d[index].facing = RL_SPRITE3D_FACING_FREE;
    rl_sprite3d[index].in_use = true;

    return handle;
}

RL_KEEP
rl_handle_t rl_sprite3d_create_from_file(const char *filename)
{
    rl_handle_t texture = rl_texture_create(filename);
    rl_handle_t handle = 0;
    uint16_t index = 0;

    if (texture == 0) {
        return 0;
    }

    handle = rl_handle_pool_alloc(&rl_sprite3d_pool);
    if (handle == 0) {
        log_error("MAX_SPRITE3D reached (%u)", MAX_SPRITE3D);
        rl_texture_destroy(texture);
        return 0;
    }
    rl_handle_pool_resolve(&rl_sprite3d_pool, handle, &index);

    rl_sprite3d[index].texture = texture;
    rl_sprite3d[index].position_x = 0.0f;
    rl_sprite3d[index].position_y = 0.0f;
    rl_sprite3d[index].position_z = 0.0f;
    rl_sprite3d[index].rotation_x = 0.0f;
    rl_sprite3d[index].rotation_y = 0.0f;
    rl_sprite3d[index].rotation_z = 0.0f;
    rl_sprite3d[index].scale_x = 1.0f;
    rl_sprite3d[index].scale_y = 1.0f;
    rl_sprite3d[index].scale_z = 1.0f;
    rl_sprite3d[index].size = 1.0f;
    rl_sprite3d[index].tint_handle = 0;
    rl_sprite3d[index].visible = true;
    rl_sprite3d[index].pickable = true;
    rl_sprite3d[index].facing = RL_SPRITE3D_FACING_FREE;
    rl_sprite3d[index].in_use = true;
    return handle;
}

RL_KEEP
bool rl_sprite3d_set_texture(rl_handle_t handle, rl_handle_t texture)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL) return false;
    if (texture != 0 && !rl_texture_retain(texture)) {
        log_warn("Invalid texture handle (%u) for rl_sprite3d_set_texture", texture);
        return false;
    }
    if (sprite->texture != 0) rl_texture_release(sprite->texture);
    sprite->texture = texture;
    return true;
}

RL_KEEP
bool rl_sprite3d_get_transform(rl_handle_t handle,
                               float *position_x, float *position_y, float *position_z,
                               float *rotation_x, float *rotation_y, float *rotation_z,
                               float *scale_x, float *scale_y, float *scale_z)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL)
        return false;
    if (position_x) *position_x = sprite->position_x;
    if (position_y) *position_y = sprite->position_y;
    if (position_z) *position_z = sprite->position_z;
    if (rotation_x) *rotation_x = sprite->rotation_x;
    if (rotation_y) *rotation_y = sprite->rotation_y;
    if (rotation_z) *rotation_z = sprite->rotation_z;
    if (scale_x)    *scale_x    = sprite->scale_x;
    if (scale_y)    *scale_y    = sprite->scale_y;
    if (scale_z)    *scale_z    = sprite->scale_z;
    return true;
}

RL_KEEP
bool rl_sprite3d_set_transform(rl_handle_t handle,
                               float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z,
                               float scale_x, float scale_y, float scale_z)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return false;
    }

    sprite->position_x = position_x;
    sprite->position_y = position_y;
    sprite->position_z = position_z;
    sprite->rotation_x = rotation_x;
    sprite->rotation_y = rotation_y;
    sprite->rotation_z = rotation_z;
    sprite->scale_x = scale_x;
    sprite->scale_y = scale_y;
    sprite->scale_z = scale_z;
    return true;
}

RL_KEEP
bool rl_sprite3d_set_size(rl_handle_t handle, float size)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL) {
        return false;
    }
    sprite->size = size;
    return true;
}

float rl_sprite3d_get_size_internal(rl_handle_t handle)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL) {
        return 0.0f;
    }
    return sprite->size;
}

RL_KEEP
bool rl_sprite3d_set_visible(rl_handle_t handle, bool visible)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    sprite->visible = visible;
    return true;
}

RL_KEEP
bool rl_sprite3d_set_pickable(rl_handle_t handle, bool pickable)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    sprite->pickable = pickable;
    return true;
}

RL_KEEP
bool rl_sprite3d_is_visible(rl_handle_t handle)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    return sprite->visible;
}

RL_KEEP
bool rl_sprite3d_is_pickable(rl_handle_t handle)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return false;
    }
    return sprite->pickable;
}

RL_KEEP
bool rl_sprite3d_has_render_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);

    if (sprite == NULL) {
        return handle != 0 && pass == RL_RENDER_PASS_TRANSPARENT_3D;
    }
    if (!sprite->visible) {
        return false;
    }
    if (sprite->texture == 0) {
        return false;
    }
    return pass == RL_RENDER_PASS_TRANSPARENT_3D;
}

RL_KEEP
void rl_sprite3d_draw_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    Texture2D *texture = NULL;
    Camera3D camera = {0};
    if (pass != RL_RENDER_PASS_TRANSPARENT_3D) {
        return;
    }
    if (sprite == NULL)
    {
        if (handle != 0 && rl_camera3d_get_active_camera(&camera)) {
            DrawBillboard(camera, *rl_texture_get_placeholder(),
                          (Vector3){0.0f, 0.0f, 0.0f}, 1.0f,
                          (Color){255, 0, 255, 255});
        }
        return;
    }
    if (!sprite->visible) {
        return;
    }
    if (sprite->texture == 0) {
        return;
    }
    texture = rl_texture_get_ptr(sprite->texture);
    if (texture == NULL) {
        log_error("Missing texture for sprite3d handle (%u)", handle);
        return;
    }
    if (!rl_camera3d_get_active_camera(&camera))
    {
        log_error("rl_sprite3d_draw() requires an active 3D camera (call rl_render_begin_mode_3d first)");
        return;
    }

    Color tint = rl_color_get(sprite->tint_handle);
    Vector3 pos = {sprite->position_x, sprite->position_y, sprite->position_z};

    switch (sprite->facing) {
        case RL_SPRITE3D_FACING_CAMERA: {
            Rectangle source = {0.0f, 0.0f, (float)texture->width, (float)texture->height};
            Vector2 size = sprite3d_billboard_size(sprite, texture);
            DrawBillboardPro(camera, *texture, source, pos, camera.up, size, Vector2Scale(size, 0.5f), 0.0f, tint);
            break;
        }
        case RL_SPRITE3D_FACING_CAMERA_FIXED_Y: {
            Rectangle source = {0.0f, 0.0f, (float)texture->width, (float)texture->height};
            Vector2 size = sprite3d_billboard_size(sprite, texture);
            DrawBillboardPro(camera, *texture, source, pos, (Vector3){0.0f, 1.0f, 0.0f}, size, Vector2Scale(size, 0.5f), 0.0f, tint);
            break;
        }
        case RL_SPRITE3D_FACING_Y_UP:
        case RL_SPRITE3D_FACING_FREE: {
            Vector3 points[4] = {0};
            Vector3 axis_a = {0};
            Vector3 axis_b = {0};
            if (sprite->facing == RL_SPRITE3D_FACING_Y_UP) {
                build_y_up_quad(pos, sprite3d_y_up_width(sprite, texture), sprite3d_y_up_length(sprite), points, &axis_a, &axis_b);
            } else {
                build_free_quad(pos, sprite3d_rotation_matrix(sprite), sprite3d_billboard_size(sprite, texture), points, &axis_a, &axis_b);
            }
            draw_textured_quad(texture, points, tint);
            break;
        }
    }
}

RL_KEEP
void rl_sprite3d_draw(rl_handle_t handle)
{
    rl_sprite3d_draw_pass(handle, RL_RENDER_PASS_TRANSPARENT_3D);
}

RL_KEEP
bool rl_sprite3d_set_facing(rl_handle_t handle, int facing)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL) return false;
    sprite->facing = (rl_sprite3d_facing_t)facing;
    return true;
}

RL_KEEP
bool rl_sprite3d_set_tint(rl_handle_t handle, rl_handle_t color_handle)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL) return false;
    sprite->tint_handle = color_handle;
    return true;
}

RL_KEEP
void rl_sprite3d_destroy(rl_handle_t handle)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    if (sprite == NULL)
    {
        return;
    }
    if (sprite->texture != 0) rl_texture_release(sprite->texture);
    sprite->texture = 0;
    sprite->in_use = false;
    rl_scene_on_drawable_destroy(handle);
    rl_handle_pool_free(&rl_sprite3d_pool, handle);
}

/* Mirrors raylib DrawBillboardPro() quad construction for camera-facing quads.
 * Keep this in sync with raylib when vendored billboard math changes. */
/* TODO: If hover/mousemove picking becomes hot, profile this path.
 * Billboard quad construction is camera-dependent, so any cache must
 * invalidate on sprite transform or camera changes. */
static bool build_camera_quad(Camera3D camera,
                              Vector3 position,
                              Vector3 up,
                              Vector2 size,
                              Vector3 points[4],
                              Vector3 *right_axis,
                              Vector3 *up_axis)
{
    Vector2 origin = Vector2Scale(size, 0.5f);
    Matrix mat_view = MatrixLookAt(camera.position, camera.target, camera.up);
    Vector3 right = {mat_view.m0, mat_view.m4, mat_view.m8};
    Vector3 origin3d = {0};

    right = Vector3Scale(right, size.x);
    up = Vector3Scale(up, size.y);

    if (size.x < 0.0f) {
        right = Vector3Negate(right);
        origin.x *= -1.0f;
    }
    if (size.y < 0.0f) {
        up = Vector3Negate(up);
        origin.y *= -1.0f;
    }

    if (Vector3Length(right) <= 0.000001f || Vector3Length(up) <= 0.000001f) {
        return false;
    }

    origin3d = Vector3Add(Vector3Scale(Vector3Normalize(right), origin.x),
                          Vector3Scale(Vector3Normalize(up), origin.y));

    points[0] = Vector3Zero();
    points[1] = right;
    points[2] = Vector3Add(up, right);
    points[3] = up;

    for (int i = 0; i < 4; i++) {
        points[i] = Vector3Subtract(points[i], origin3d);
        points[i] = Vector3Add(points[i], position);
    }

    if (right_axis != NULL) *right_axis = right;
    if (up_axis != NULL) *up_axis = up;
    return true;
}

static bool build_camera_fixed_y_quad(Camera3D camera,
                                      Vector3 position,
                                      Vector2 size,
                                      Vector3 points[4],
                                      Vector3 *right_axis,
                                      Vector3 *up_axis)
{
    return build_camera_quad(camera,
                             position,
                             (Vector3){0.0f, 1.0f, 0.0f},
                             size,
                             points,
                             right_axis,
                             up_axis);
}

static void build_y_up_quad(Vector3 position,
                            float width,
                            float length,
                            Vector3 points[4],
                            Vector3 *right_axis,
                            Vector3 *forward_axis)
{
    float half_width = width * 0.5f;
    float half_length = length * 0.5f;
    Vector3 right = {width, 0.0f, 0.0f};
    Vector3 forward = {0.0f, 0.0f, length};

    points[0] = (Vector3){position.x - half_width, position.y, position.z + half_length};
    points[1] = (Vector3){position.x + half_width, position.y, position.z + half_length};
    points[2] = (Vector3){position.x + half_width, position.y, position.z - half_length};
    points[3] = (Vector3){position.x - half_width, position.y, position.z - half_length};

    if (right_axis != NULL) *right_axis = right;
    if (forward_axis != NULL) *forward_axis = forward;
}

static void build_free_quad(Vector3 position,
                            Matrix rotation,
                            Vector2 size,
                            Vector3 points[4],
                            Vector3 *right_axis,
                            Vector3 *up_axis)
{
    float half_width = size.x * 0.5f;
    float half_height = size.y * 0.5f;
    Vector3 local_points[4] = {
        {-half_width, -half_height, 0.0f},
        { half_width, -half_height, 0.0f},
        { half_width,  half_height, 0.0f},
        {-half_width,  half_height, 0.0f}
    };
    Vector3 right = Vector3Transform((Vector3){size.x, 0.0f, 0.0f}, rotation);
    Vector3 up = Vector3Transform((Vector3){0.0f, size.y, 0.0f}, rotation);
    int i = 0;

    for (i = 0; i < 4; i++) {
        points[i] = Vector3Add(position, Vector3Transform(local_points[i], rotation));
    }
    if (right_axis != NULL) *right_axis = right;
    if (up_axis != NULL) *up_axis = up;
}

bool rl_sprite3d_get_ray_collision_ex(rl_handle_t handle,
                                      Camera3D camera,
                                      Ray ray,
                                      float position_x,
                                      float position_y,
                                      float position_z,
                                      float rotation_x,
                                      float rotation_y,
                                      float rotation_z,
                                      float scale_x,
                                      float scale_y,
                                      float scale_z,
                                      float size,
                                      RayCollision *collision,
                                      bool *broadphase_tested,
                                      bool *broadphase_rejected,
                                      bool *narrowphase_ran)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    Texture2D *texture = NULL;
    Vector3 position = {position_x, position_y, position_z};
    Vector3 points[4] = {0};
    Vector3 right_axis = {0};
    Vector3 up_axis = {0};
    Vector2 billboard_size = {0};
    float radius = 0.0f;
    Matrix rotation = MatrixRotateXYZ((Vector3){rotation_x, rotation_y, rotation_z});

    if (collision == NULL) {
        return false;
    }
    if (broadphase_tested != NULL) *broadphase_tested = false;
    if (broadphase_rejected != NULL) *broadphase_rejected = false;
    if (narrowphase_ran != NULL) *narrowphase_ran = false;
    *collision = (RayCollision){0};

    if (sprite == NULL) {
        return false;
    }

    texture = rl_texture_get_ptr(sprite->texture);
    if (texture == NULL || texture->height == 0 || fabsf(size) <= 0.00001f) {
        return false;
    }

    {
        float width = 0.0f;
        float height = 0.0f;

        if (sprite->facing == RL_SPRITE3D_FACING_Y_UP) {
            width = fabsf(sprite3d_texture_aspect(texture) * scale_x * size);
            height = fabsf(scale_z * size);
        } else {
            width = fabsf(sprite3d_texture_aspect(texture) * scale_x * size);
            height = fabsf(scale_y * size);
        }

        radius = sqrtf((0.5f * width * 0.5f * width) + (0.5f * height * 0.5f * height));
        if (broadphase_tested != NULL) *broadphase_tested = true;
        RayCollision broad_hit = GetRayCollisionSphere(ray, position, radius);
        if (!broad_hit.hit) {
            if (broadphase_rejected != NULL) *broadphase_rejected = true;
            return true;
        }
    }

    billboard_size = (Vector2){sprite3d_texture_aspect(texture) * scale_x * size, scale_y * size};
    if (sprite->facing == RL_SPRITE3D_FACING_CAMERA) {
        if (!build_camera_quad(camera, position, camera.up, billboard_size, points, &right_axis, &up_axis)) {
            return false;
        }
    } else if (sprite->facing == RL_SPRITE3D_FACING_CAMERA_FIXED_Y) {
        if (!build_camera_fixed_y_quad(camera, position, billboard_size, points, &right_axis, &up_axis)) {
            return false;
        }
    } else if (sprite->facing == RL_SPRITE3D_FACING_Y_UP) {
        build_y_up_quad(position,
                        sprite3d_texture_aspect(texture) * scale_x * size,
                        scale_z * size,
                        points,
                        &right_axis,
                        &up_axis);
    } else {
        build_free_quad(position, rotation, billboard_size, points, &right_axis, &up_axis);
    }

    if (narrowphase_ran != NULL) *narrowphase_ran = true;
    *collision = GetRayCollisionQuad(ray, points[0], points[1], points[2], points[3]);
    if (collision->hit) {
        Vector3 offset = Vector3Subtract(collision->point, position);
        Vector3 right_dir = Vector3Normalize(right_axis);
        Vector3 up_dir = Vector3Normalize(up_axis);
        float local_x = Vector3DotProduct(offset, right_dir);
        float local_y = Vector3DotProduct(offset, up_dir);
        if (sprite->facing == RL_SPRITE3D_FACING_CAMERA ||
            sprite->facing == RL_SPRITE3D_FACING_CAMERA_FIXED_Y) {
            collision->point = (Vector3){local_x, local_y, 0.0f};
            collision->normal = (Vector3){0.0f, 0.0f, (collision->normal.z < 0.0f) ? -1.0f : 1.0f};
        } else if (sprite->facing == RL_SPRITE3D_FACING_Y_UP) {
            collision->point = (Vector3){local_x, 0.0f, local_y};
            collision->normal = (Vector3){0.0f, (collision->normal.y < 0.0f) ? -1.0f : 1.0f, 0.0f};
        } else {
            collision->point = (Vector3){local_x, local_y, 0.0f};
            collision->normal = (Vector3){0.0f, 0.0f, (Vector3DotProduct(collision->normal, Vector3Normalize(Vector3CrossProduct(right_axis, up_axis))) < 0.0f) ? -1.0f : 1.0f};
        }
    }
    return true;
}

bool rl_sprite3d_scene_pick_broadphase(rl_handle_t handle, Ray ray)
{
    rl_sprite3d_instance_t *sprite = resolve_sprite3d_instance(handle);
    Texture2D *texture = NULL;
    Vector3 position = {0};
    float radius = 0.0f;
    RayCollision broad_hit = {0};

    if (sprite == NULL) {
        return false;
    }
    if (!sprite->pickable) {
        return false;
    }

    texture = rl_texture_get_ptr(sprite->texture);
    if (texture == NULL || texture->height == 0 || fabsf(sprite->size) <= 0.00001f) {
        return false;
    }

    if (sprite->facing == RL_SPRITE3D_FACING_Y_UP) {
        float width = fabsf(sprite3d_y_up_width(sprite, texture));
        float length = fabsf(sprite3d_y_up_length(sprite));
        radius = sqrtf((0.5f * width * 0.5f * width) + (0.5f * length * 0.5f * length));
    } else {
        Vector2 billboard_size = sprite3d_billboard_size(sprite, texture);
        float half_width = fabsf(billboard_size.x) * 0.5f;
        float half_height = fabsf(billboard_size.y) * 0.5f;
        radius = sqrtf((half_width * half_width) + (half_height * half_height));
    }
    position = (Vector3){sprite->position_x, sprite->position_y, sprite->position_z};
    broad_hit = GetRayCollisionSphere(ray, position, radius);
    return broad_hit.hit;
}

bool rl_sprite3d_get_ray_collision(rl_handle_t handle,
                                   Camera3D camera,
                                   Ray ray,
                                   float position_x,
                                   float position_y,
                                   float position_z,
                                   float rotation_x,
                                   float rotation_y,
                                   float rotation_z,
                                   float scale_x,
                                   float scale_y,
                                   float scale_z,
                                   float size,
                                   RayCollision *collision)
{
    return rl_sprite3d_get_ray_collision_ex(handle,
                                            camera,
                                            ray,
                                            position_x,
                                            position_y,
                                            position_z,
                                            rotation_x,
                                            rotation_y,
                                            rotation_z,
                                            scale_x,
                                            scale_y,
                                            scale_z,
                                            size,
                                            collision,
                                            NULL,
                                            NULL,
                                            NULL);
}

void rl_sprite3d_init(void)
{
    rl_handle_pool_init(&rl_sprite3d_pool,
                        RL_HANDLE_KIND_SPRITE3D,
                        MAX_SPRITE3D,
                        rl_sprite3d_free_indices,
                        MAX_SPRITE3D,
                        rl_sprite3d_generations,
                        rl_sprite3d_occupied);
    for (int i = 0; i < MAX_SPRITE3D; i++)
    {
        rl_sprite3d[i].in_use = false;
        rl_sprite3d[i].texture = 0;
        rl_sprite3d[i].position_x = 0.0f;
        rl_sprite3d[i].position_y = 0.0f;
        rl_sprite3d[i].position_z = 0.0f;
        rl_sprite3d[i].rotation_x = 0.0f;
        rl_sprite3d[i].rotation_y = 0.0f;
        rl_sprite3d[i].rotation_z = 0.0f;
        rl_sprite3d[i].scale_x = 1.0f;
        rl_sprite3d[i].scale_y = 1.0f;
        rl_sprite3d[i].scale_z = 1.0f;
        rl_sprite3d[i].size = 1.0f;
        rl_sprite3d[i].tint_handle = 0;
        rl_sprite3d[i].visible = true;
        rl_sprite3d[i].pickable = true;
        rl_sprite3d[i].facing = RL_SPRITE3D_FACING_FREE;
    }
}

void rl_sprite3d_deinit(void)
{
    int unloaded = 0;
    for (uint16_t i = 1; i < MAX_SPRITE3D; i++)
    {
        if (!rl_sprite3d[i].in_use) {
            continue;
        }
        {
            rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_sprite3d_pool, i);
            if (handle == 0) {
                continue;
            }
            rl_sprite3d_destroy(handle);
            unloaded++;
        }
    }
    rl_handle_pool_reset(&rl_sprite3d_pool);
    log_info("rl_sprite3d_deinit: Freed %d sprite3d textures", unloaded);
}
