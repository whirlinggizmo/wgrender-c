#include "rl_pick.h"

#include <raylib.h>
#include <raymath.h>
#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_model.h"
#include "internal/rl_shape.h"
#include "internal/rl_sprite3d.h"
#include "internal/rl_text3d.h"
#include "rl_model.h"
#include "rl_scratch.h"
#include "rl_sprite3d.h"

static rl_pick_stats_t rl_pick_stats = {0};

static rl_pick_result_t make_empty_pick_result(void)
{
    rl_pick_result_t result = {0};
    result.hit = false;
    result.handle = 0;
    result.distance = -1.0f;
    result.point = (vec3_t){0.0f, 0.0f, 0.0f};
    result.normal = (vec3_t){0.0f, 0.0f, 0.0f};
    return result;
}

static Matrix build_model_pick_matrix(float x, float y, float z,
                                      float scale_x, float scale_y, float scale_z,
                                      float rotation_x, float rotation_y, float rotation_z)
{
    Matrix translation = MatrixTranslate(x, y, z);
    Matrix rotation = MatrixRotateXYZ((Vector3){
        rotation_x,
        rotation_y,
        rotation_z
    });
    Matrix scaling = MatrixScale(scale_x, scale_y, scale_z);
    return MatrixMultiply(MatrixMultiply(scaling, rotation), translation);
}

static Vector3 transform_direction(Matrix transform, Vector3 direction)
{
    return (Vector3){
        transform.m0*direction.x + transform.m4*direction.y + transform.m8*direction.z,
        transform.m1*direction.x + transform.m5*direction.y + transform.m9*direction.z,
        transform.m2*direction.x + transform.m6*direction.y + transform.m10*direction.z
    };
}

static rl_pick_result_t pick_result_from_ray_collision(RayCollision collision)
{
    rl_pick_result_t result = make_empty_pick_result();
    if (!collision.hit) {
        return result;
    }

    result.hit = true;
    result.distance = collision.distance;
    result.point = (vec3_t){collision.point.x, collision.point.y, collision.point.z};
    result.normal = (vec3_t){collision.normal.x, collision.normal.y, collision.normal.z};
    return result;
}

RL_KEEP
rl_pick_result_t rl_pick_model_with_camera_ray(Camera3D camera_data,
                                               Ray ray,
                                               rl_handle_t model)
{
    (void)camera_data;
    Matrix instance_transform = {0};
    Matrix model_transform = {0};
    RayCollision collision = {0};
    bool broadphase_tested = false;
    bool broadphase_rejected = false;
    bool narrowphase_ran = false;
    float position_x = 0, position_y = 0, position_z = 0;
    float scale_x = 1, scale_y = 1, scale_z = 1;
    float rotation_x = 0, rotation_y = 0, rotation_z = 0;

    if (!rl_model_is_pickable(model)) {
        return make_empty_pick_result();
    }

    if (!rl_model_get_transform(model, &position_x, &position_y, &position_z,
                                &scale_x, &scale_y, &scale_z,
                                &rotation_x, &rotation_y, &rotation_z)) {
        return make_empty_pick_result();
    }

    instance_transform = build_model_pick_matrix(position_x, position_y, position_z,
                                                 scale_x, scale_y, scale_z,
                                                 rotation_x, rotation_y, rotation_z);
    if (!rl_model_get_ray_collision_ex(model,
                                       ray,
                                       instance_transform,
                                       &collision,
                                       &model_transform,
                                       &broadphase_tested,
                                       &broadphase_rejected,
                                       &narrowphase_ran)) {
        return make_empty_pick_result();
    }

    if (broadphase_tested) {
        rl_pick_stats.broadphase_tests++;
    }
    if (broadphase_rejected) {
        rl_pick_stats.broadphase_rejects++;
    }
    if (narrowphase_ran) {
        rl_pick_stats.narrowphase_tests++;
    }
    if (collision.hit && narrowphase_ran) {
        rl_pick_stats.narrowphase_hits++;
    }

    if (collision.hit) {
        Matrix inv = MatrixInvert(model_transform);
        Matrix local_normal_transform = MatrixTranspose(model_transform);
        Vector3 world_pt = {collision.point.x, collision.point.y, collision.point.z};
        Vector3 world_normal = {collision.normal.x, collision.normal.y, collision.normal.z};
        Vector3 local_pt = Vector3Transform(world_pt, inv);
        Vector3 local_normal = Vector3Normalize(
            transform_direction(local_normal_transform, world_normal)
        );
        collision.point = local_pt;
        collision.normal = local_normal;
    }

    return pick_result_from_ray_collision(collision);
}

RL_KEEP
rl_pick_result_t rl_pick_sprite3d_with_camera_ray(Camera3D camera_data,
                                                 Ray ray,
                                                 rl_handle_t sprite3d)
{
    RayCollision collision = {0};
    bool broadphase_tested = false;
    bool broadphase_rejected = false;
    bool narrowphase_ran = false;
    float position_x = 0, position_y = 0, position_z = 0;
    float rotation_x = 0, rotation_y = 0, rotation_z = 0;
    float scale_x = 1, scale_y = 1, scale_z = 1;
    float size = 1.0f;

    if (!rl_sprite3d_is_pickable(sprite3d)) {
        return make_empty_pick_result();
    }

    if (!rl_sprite3d_get_transform(sprite3d,
                                   &position_x, &position_y, &position_z,
                                   &rotation_x, &rotation_y, &rotation_z,
                                   &scale_x, &scale_y, &scale_z)) {
        return make_empty_pick_result();
    }
    size = rl_sprite3d_get_size_internal(sprite3d);

    if (!rl_sprite3d_get_ray_collision_ex(sprite3d,
                                          camera_data,
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
                                          &collision,
                                          &broadphase_tested,
                                          &broadphase_rejected,
                                          &narrowphase_ran)) {
        return make_empty_pick_result();
    }

    if (broadphase_tested) {
        rl_pick_stats.broadphase_tests++;
    }
    if (broadphase_rejected) {
        rl_pick_stats.broadphase_rejects++;
    }
    if (narrowphase_ran) {
        rl_pick_stats.narrowphase_tests++;
    }
    if (collision.hit && narrowphase_ran) {
        rl_pick_stats.narrowphase_hits++;
    }

    return pick_result_from_ray_collision(collision);
}

RL_KEEP
rl_pick_result_t rl_pick_model(rl_handle_t camera,
                               rl_handle_t model,
                               float mouse_x,
                               float mouse_y)
{
    Camera3D camera_data = {0};
    Ray ray = {0};

    if (!rl_camera3d_get_camera(camera, &camera_data)) {
        return make_empty_pick_result();
    }

    ray = GetMouseRay((Vector2){mouse_x, mouse_y}, camera_data);
    rl_pick_result_t result = rl_pick_model_with_camera_ray(camera_data, ray, model);
    if (result.hit) {
        result.handle = model;
    }
    return result;
}

RL_KEEP
bool rl_pick_model_to_scratch(rl_handle_t camera,
                              rl_handle_t model,
                              float mouse_x,
                              float mouse_y)
{
    rl_pick_result_t result = rl_pick_model(camera, model, mouse_x, mouse_y);

    rl_scratch_set_pick_result(result);
    return result.hit;
}

RL_KEEP
rl_pick_result_t rl_pick_sprite3d(rl_handle_t camera,
                                  rl_handle_t sprite3d,
                                  float mouse_x,
                                  float mouse_y)
{
    Camera3D camera_data = {0};
    Ray ray = {0};

    if (!rl_camera3d_get_camera(camera, &camera_data)) {
        return make_empty_pick_result();
    }

    ray = GetMouseRay((Vector2){mouse_x, mouse_y}, camera_data);
    rl_pick_result_t result = rl_pick_sprite3d_with_camera_ray(camera_data, ray, sprite3d);
    if (result.hit) {
        result.handle = sprite3d;
    }
    return result;
}

RL_KEEP
bool rl_pick_sprite3d_to_scratch(rl_handle_t camera,
                                 rl_handle_t sprite3d,
                                 float mouse_x,
                                 float mouse_y)
{
    rl_pick_result_t result = rl_pick_sprite3d(camera, sprite3d, mouse_x, mouse_y);

    rl_scratch_set_pick_result(result);
    return result.hit;
}

RL_KEEP
rl_pick_result_t rl_pick_shape_with_camera_ray(Camera3D camera_data,
                                               Ray ray,
                                               rl_handle_t shape)
{
    RayCollision collision = {0};
    (void)camera_data;

    if (!rl_shape_is_pickable(shape)) {
        return make_empty_pick_result();
    }

    rl_pick_stats.narrowphase_tests++;
    collision = rl_shape_get_ray_collision(shape, ray);
    if (collision.hit) {
        rl_pick_stats.narrowphase_hits++;
    }

    return pick_result_from_ray_collision(collision);
}

RL_KEEP
rl_pick_result_t rl_pick_shape(rl_handle_t camera,
                               rl_handle_t shape,
                               float mouse_x,
                               float mouse_y)
{
    Camera3D camera_data = {0};
    Ray ray = {0};

    if (!rl_camera3d_get_camera(camera, &camera_data)) {
        return make_empty_pick_result();
    }

    ray = GetMouseRay((Vector2){mouse_x, mouse_y}, camera_data);
    rl_pick_result_t result = rl_pick_shape_with_camera_ray(camera_data, ray, shape);
    if (result.hit) {
        result.handle = shape;
    }
    return result;
}

RL_KEEP
bool rl_pick_shape_to_scratch(rl_handle_t camera,
                              rl_handle_t shape,
                              float mouse_x,
                              float mouse_y)
{
    rl_pick_result_t result = rl_pick_shape(camera, shape, mouse_x, mouse_y);

    rl_scratch_set_pick_result(result);
    return result.hit;
}

RL_KEEP
rl_pick_result_t rl_pick_text3d_with_camera_ray(Camera3D camera_data,
                                                Ray ray,
                                                rl_handle_t text3d)
{
    RayCollision collision = {0};

    if (!rl_text3d_is_pickable_internal(text3d)) {
        return make_empty_pick_result();
    }

    rl_pick_stats.broadphase_tests++;
    if (!rl_text3d_scene_pick_broadphase(text3d, ray)) {
        rl_pick_stats.broadphase_rejects++;
        return make_empty_pick_result();
    }

    rl_pick_stats.narrowphase_tests++;
    collision = rl_text3d_get_ray_collision(text3d, camera_data, ray);
    if (collision.hit) {
        rl_pick_stats.narrowphase_hits++;
    }

    return pick_result_from_ray_collision(collision);
}

RL_KEEP
rl_pick_result_t rl_pick_text3d(rl_handle_t camera,
                                rl_handle_t text3d,
                                float mouse_x,
                                float mouse_y)
{
    Camera3D camera_data = {0};
    Ray ray = {0};

    if (!rl_camera3d_get_camera(camera, &camera_data)) {
        return make_empty_pick_result();
    }

    ray = GetMouseRay((Vector2){mouse_x, mouse_y}, camera_data);
    rl_pick_result_t result = rl_pick_text3d_with_camera_ray(camera_data, ray, text3d);
    if (result.hit) {
        result.handle = text3d;
    }
    return result;
}

RL_KEEP
bool rl_pick_text3d_to_scratch(rl_handle_t camera,
                               rl_handle_t text3d,
                               float mouse_x,
                               float mouse_y)
{
    rl_pick_result_t result = rl_pick_text3d(camera, text3d, mouse_x, mouse_y);

    rl_scratch_set_pick_result(result);
    return result.hit;
}

RL_KEEP
void rl_pick_reset_stats(void)
{
    rl_pick_stats = (rl_pick_stats_t){0};
}

RL_KEEP
int rl_pick_get_broadphase_tests(void)
{
    return rl_pick_stats.broadphase_tests;
}

RL_KEEP
int rl_pick_get_broadphase_rejects(void)
{
    return rl_pick_stats.broadphase_rejects;
}

RL_KEEP
int rl_pick_get_narrowphase_tests(void)
{
    return rl_pick_stats.narrowphase_tests;
}

RL_KEEP
int rl_pick_get_narrowphase_hits(void)
{
    return rl_pick_stats.narrowphase_hits;
}
