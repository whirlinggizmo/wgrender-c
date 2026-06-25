#include "rl_scene.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <raylib.h>

#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_model.h"
#include "internal/rl_render_pass.h"
#include "internal/rl_pick_scene.h"
#include "internal/rl_scene.h"
#include "internal/rl_shape.h"
#include "internal/rl_sprite3d.h"
#include "internal/rl_text3d.h"
#include "rl_camera3d.h"
#include "rl_handle.h"
#include "rl_model.h"
#include "rl_pick.h"
#include "rl_render.h"
#include "rl_scratch.h"
#include "rl_sprite2d.h"
#include "rl_sprite3d.h"
#include "rl_text2d.h"
#include "rl_text3d.h"

#include <rlgl.h>


#define MAX_SCENES 32
#define MAX_SCENE_MEMBERS 128

typedef struct
{
    rl_handle_t drawable;
    rl_handle_kind_t kind;
    int layer;
    uint32_t order;
} rl_scene_member_t;

typedef struct
{
    rl_handle_t drawable;
    rl_handle_kind_t kind;
    uint32_t order;
    float depth_key;
} rl_scene_draw_item_t;

typedef struct
{
    bool in_use;
    rl_handle_t camera;
    rl_scene_member_t members[MAX_SCENE_MEMBERS];
    int member_count;
    uint32_t next_order;
    bool order_dirty;
    rl_scene_member_t sorted[MAX_SCENE_MEMBERS];
    rl_scene_draw_item_t opaque3d[MAX_SCENE_MEMBERS];
    int opaque3d_count;
    rl_scene_draw_item_t transparent3d[MAX_SCENE_MEMBERS];
    int transparent3d_count;
    rl_scene_draw_item_t overlay2d[MAX_SCENE_MEMBERS];
    int overlay2d_count;
} rl_scene_instance_t;

static rl_scene_instance_t rl_scenes[MAX_SCENES];
static rl_handle_pool_t rl_scene_pool;
static uint16_t rl_scene_free_indices[MAX_SCENES];
static uint16_t rl_scene_generations[MAX_SCENES];
static unsigned char rl_scene_occupied[MAX_SCENES];

static rl_scene_instance_t *resolve_scene_instance(rl_handle_t scene)
{
    uint16_t index = 0;

    if (!rl_handle_pool_resolve(&rl_scene_pool, scene, &index)) {
        if (scene != 0) {
            log_warn("Invalid scene handle (%u)", (unsigned int)scene);
        }
        return NULL;
    }
    if (!rl_scenes[index].in_use) {
        log_warn("Stale scene handle (%u)", (unsigned int)scene);
        return NULL;
    }
    return &rl_scenes[index];
}

static bool is_drawable_kind(rl_handle_kind_t kind)
{
    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
    case RL_HANDLE_KIND_SPRITE3D:
    case RL_HANDLE_KIND_SPRITE2D:
    case RL_HANDLE_KIND_TEXT2D:
    case RL_HANDLE_KIND_SHAPE:
    case RL_HANDLE_KIND_TEXT3D:
        return true;
    default:
        return false;
    }
}

static bool is_drawable_handle_valid(rl_handle_t drawable, rl_handle_kind_t kind)
{
    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
        return rl_model_is_valid(drawable);
    case RL_HANDLE_KIND_SPRITE3D:
    case RL_HANDLE_KIND_SPRITE2D:
    case RL_HANDLE_KIND_TEXT2D:
        return true;
    case RL_HANDLE_KIND_SHAPE:
        return rl_shape_is_valid(drawable);
    case RL_HANDLE_KIND_TEXT3D:
        return rl_text3d_is_valid(drawable);
    default:
        return false;
    }
}

static int find_member_index(const rl_scene_instance_t *scene, rl_handle_t drawable)
{
    int i = 0;

    if (scene == NULL) {
        return -1;
    }

    for (i = 0; i < scene->member_count; i++) {
        if (scene->members[i].drawable == drawable) {
            return i;
        }
    }
    return -1;
}

static rl_handle_t resolve_scene_camera_handle(const rl_scene_instance_t *scene,
                                           rl_handle_t camera_param)
{
    if (camera_param != 0) {
        return camera_param;
    }
    if (scene != NULL && scene->camera != 0) {
        return scene->camera;
    }
    return rl_camera3d_get_active();
}

static void apply_scene_camera(rl_handle_t camera)
{
    if (camera != 0) {
        (void)rl_camera3d_set_active(camera);
    }
}

static int compare_scene_members(const void *lhs_ptr, const void *rhs_ptr)
{
    const rl_scene_member_t *lhs = (const rl_scene_member_t *)lhs_ptr;
    const rl_scene_member_t *rhs = (const rl_scene_member_t *)rhs_ptr;

    if (lhs->layer != rhs->layer) {
        return (lhs->layer < rhs->layer) ? -1 : 1;
    }
    if (lhs->order != rhs->order) {
        return (lhs->order < rhs->order) ? -1 : 1;
    }
    return 0;
}

static void sort_scene_members(rl_scene_member_t *members, int count)
{
    if (count <= 1) {
        return;
    }
    qsort(members, (size_t)count, sizeof(members[0]), compare_scene_members);
}

static void refresh_sorted_members(rl_scene_instance_t *scene)
{
    if (scene == NULL || !scene->order_dirty) {
        return;
    }

    memcpy(scene->sorted, scene->members,
           (size_t)scene->member_count * sizeof(scene->sorted[0]));
    sort_scene_members(scene->sorted, scene->member_count);
    scene->order_dirty = false;
}

static Vector3 scene_camera_forward(Camera3D camera)
{
    Vector3 forward = {
        camera.target.x - camera.position.x,
        camera.target.y - camera.position.y,
        camera.target.z - camera.position.z
    };
    float length = sqrtf(forward.x * forward.x +
                         forward.y * forward.y +
                         forward.z * forward.z);

    if (length <= 0.00001f) {
        return (Vector3){0.0f, 0.0f, -1.0f};
    }

    forward.x /= length;
    forward.y /= length;
    forward.z /= length;
    return forward;
}

static bool get_drawable_world_position(rl_handle_t drawable,
                                        rl_handle_kind_t kind,
                                        Vector3 *position)
{
    float position_x = 0.0f;
    float position_y = 0.0f;
    float position_z = 0.0f;
    float rotation_x = 0.0f;
    float rotation_y = 0.0f;
    float rotation_z = 0.0f;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float scale_z = 1.0f;

    if (position == NULL) {
        return false;
    }

    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
        if (!rl_model_get_transform(drawable,
                                    &position_x, &position_y, &position_z,
                                    &scale_x, &scale_y, &scale_z,
                                    &rotation_x, &rotation_y, &rotation_z)) {
            return false;
        }
        break;
    case RL_HANDLE_KIND_SPRITE3D:
        if (!rl_sprite3d_get_transform(drawable,
                                       &position_x, &position_y, &position_z,
                                       &rotation_x, &rotation_y, &rotation_z,
                                       &scale_x, &scale_y, &scale_z)) {
            return false;
        }
        break;
    case RL_HANDLE_KIND_TEXT3D:
        if (!rl_text3d_get_position(drawable, &position_x, &position_y, &position_z)) {
            return false;
        }
        break;
    default:
        return false;
    }

    *position = (Vector3){position_x, position_y, position_z};
    return true;
}

static float compute_transparent_depth_key(Camera3D camera,
                                           Vector3 camera_forward,
                                           rl_handle_t drawable,
                                           rl_handle_kind_t kind)
{
    Vector3 position = {0};
    Vector3 offset = {0};

    if (!get_drawable_world_position(drawable, kind, &position)) {
        return 0.0f;
    }

    offset.x = position.x - camera.position.x;
    offset.y = position.y - camera.position.y;
    offset.z = position.z - camera.position.z;

    return offset.x * camera_forward.x +
           offset.y * camera_forward.y +
           offset.z * camera_forward.z;
}

static int compare_transparent_draw_items(const void *lhs_ptr, const void *rhs_ptr)
{
    const rl_scene_draw_item_t *lhs = (const rl_scene_draw_item_t *)lhs_ptr;
    const rl_scene_draw_item_t *rhs = (const rl_scene_draw_item_t *)rhs_ptr;

    if (lhs->depth_key > rhs->depth_key) {
        return -1;
    }
    if (lhs->depth_key < rhs->depth_key) {
        return 1;
    }
    if (lhs->order != rhs->order) {
        return (lhs->order < rhs->order) ? -1 : 1;
    }
    return 0;
}

static void sort_transparent_draw_items(rl_scene_draw_item_t *items, int count)
{
    if (count <= 1) {
        return;
    }
    qsort(items, (size_t)count, sizeof(items[0]), compare_transparent_draw_items);
}

static void draw_scene_member(rl_handle_t drawable, rl_handle_kind_t kind)
{
    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
        rl_model_draw(drawable);
        break;
    case RL_HANDLE_KIND_SPRITE3D:
        rl_sprite3d_draw(drawable);
        break;
    case RL_HANDLE_KIND_SPRITE2D:
        rl_sprite2d_draw(drawable);
        break;
    case RL_HANDLE_KIND_TEXT2D:
        rl_text2d_draw(drawable);
        break;
    case RL_HANDLE_KIND_SHAPE:
        rl_shape_draw(drawable);
        break;
    default:
        break;
    }
}

static void draw_scene_member_pass(rl_handle_t drawable,
                                   rl_handle_kind_t kind,
                                   rl_render_pass_t pass)
{
    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
        rl_model_draw_pass(drawable, pass);
        break;
    case RL_HANDLE_KIND_SPRITE3D:
        rl_sprite3d_draw_pass(drawable, pass);
        break;
    case RL_HANDLE_KIND_SHAPE:
        rl_shape_draw_pass(drawable, pass);
        break;
    case RL_HANDLE_KIND_TEXT3D:
        rl_text3d_draw_pass(drawable, pass);
        break;
    default:
        break;
    }
}

static bool is_kind_narrow_pickable(rl_handle_kind_t kind)
{
    return kind == RL_HANDLE_KIND_MODEL ||
           kind == RL_HANDLE_KIND_SPRITE3D ||
           kind == RL_HANDLE_KIND_SHAPE ||
           kind == RL_HANDLE_KIND_TEXT3D;
}

static bool is_drawable_visible(rl_handle_t drawable, rl_handle_kind_t kind)
{
    switch (kind) {
    case RL_HANDLE_KIND_MODEL:
        return rl_model_is_visible(drawable);
    case RL_HANDLE_KIND_SPRITE3D:
        return rl_sprite3d_is_visible(drawable);
    case RL_HANDLE_KIND_SPRITE2D:
        return rl_sprite2d_is_visible(drawable);
    case RL_HANDLE_KIND_TEXT2D:
        return rl_text2d_is_visible(drawable);
    case RL_HANDLE_KIND_SHAPE:
        return rl_shape_is_visible(drawable);
    case RL_HANDLE_KIND_TEXT3D:
        return rl_text3d_is_visible(drawable);
    default:
        return false;
    }
}

RL_KEEP
rl_handle_t rl_scene_create(void)
{
    rl_handle_t handle = rl_handle_pool_alloc(&rl_scene_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_SCENES reached (%d)", MAX_SCENES);
        return 0;
    }

    rl_handle_pool_resolve(&rl_scene_pool, handle, &index);
    rl_scenes[index].camera = 0;
    rl_scenes[index].member_count = 0;
    rl_scenes[index].next_order = 0;
    rl_scenes[index].order_dirty = false;
    rl_scenes[index].opaque3d_count = 0;
    rl_scenes[index].transparent3d_count = 0;
    rl_scenes[index].overlay2d_count = 0;
    rl_scenes[index].in_use = true;
    return handle;
}

RL_KEEP
void rl_scene_destroy(rl_handle_t scene)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);

    if (instance == NULL) {
        return;
    }

    instance->camera = 0;
    instance->member_count = 0;
    instance->next_order = 0;
    instance->order_dirty = false;
    instance->opaque3d_count = 0;
    instance->transparent3d_count = 0;
    instance->overlay2d_count = 0;
    instance->in_use = false;
    rl_handle_pool_free(&rl_scene_pool, scene);
}

RL_KEEP
bool rl_scene_add(rl_handle_t scene, rl_handle_t drawable, int layer)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);
    rl_handle_kind_t kind = RL_HANDLE_KIND_NONE;
    rl_scene_member_t *member = NULL;

    if (instance == NULL || drawable == 0) {
        return false;
    }

    kind = rl_handle_get_kind(drawable);
    if (!is_drawable_kind(kind)) {
        log_warn("rl_scene_add: unsupported drawable kind (%d) for handle (%u)",
                 (int)kind, (unsigned int)drawable);
        return false;
    }

    if (find_member_index(instance, drawable) >= 0) {
        return false;
    }

    if (instance->member_count >= MAX_SCENE_MEMBERS) {
        log_error("rl_scene_add: MAX_SCENE_MEMBERS reached (%d)", MAX_SCENE_MEMBERS);
        return false;
    }

    member = &instance->members[instance->member_count++];
    member->drawable = drawable;
    member->kind = kind;
    member->layer = layer;
    member->order = instance->next_order++;
    instance->order_dirty = true;
    return true;
}

RL_KEEP
bool rl_scene_set_layer(rl_handle_t scene, rl_handle_t drawable, int layer)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);
    int index = 0;

    if (instance == NULL || drawable == 0) {
        return false;
    }

    index = find_member_index(instance, drawable);
    if (index < 0) {
        return false;
    }

    instance->members[index].layer = layer;
    instance->order_dirty = true;
    return true;
}

RL_KEEP
bool rl_scene_remove(rl_handle_t scene, rl_handle_t drawable)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);
    int index = 0;

    if (instance == NULL) {
        return false;
    }

    index = find_member_index(instance, drawable);
    if (index < 0) {
        return false;
    }

    instance->member_count--;
    if (index < instance->member_count) {
        instance->members[index] = instance->members[instance->member_count];
    }
    instance->order_dirty = true;
    return true;
}

RL_KEEP
void rl_scene_clear(rl_handle_t scene)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);

    if (instance == NULL) {
        return;
    }

    instance->member_count = 0;
    instance->next_order = 0;
    instance->order_dirty = false;
    instance->opaque3d_count = 0;
    instance->transparent3d_count = 0;
    instance->overlay2d_count = 0;
}

RL_KEEP
void rl_scene_set_active_camera(rl_handle_t scene, rl_handle_t camera)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);

    if (instance == NULL) {
        return;
    }

    instance->camera = camera;
}

RL_KEEP
void rl_scene_draw(rl_handle_t scene)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);
    rl_handle_t camera = 0;
    Camera3D camera_data = {0};
    Vector3 camera_forward = {0.0f, 0.0f, -1.0f};
    int i = 0;
    bool has_3d = false;
    bool has_2d = false;

    if (instance == NULL) {
        return;
    }

    if (instance->member_count <= 0) {
        return;
    }

    refresh_sorted_members(instance);

    camera = resolve_scene_camera_handle(instance, 0);
    apply_scene_camera(camera);
    if (has_3d || instance->member_count > 0) {
        (void)rl_camera3d_get_active_camera(&camera_data);
        camera_forward = scene_camera_forward(camera_data);
    }

    instance->overlay2d_count = 0;

    for (i = 0; i < instance->member_count; i++) {
        rl_scene_member_t *member = &instance->sorted[i];
        rl_scene_draw_item_t item = {0};

        if (!is_drawable_visible(member->drawable, member->kind)) {
            continue;
        }
        if (!is_drawable_handle_valid(member->drawable, member->kind)) {
            continue;
        }

        item.drawable = member->drawable;
        item.kind = member->kind;
        item.order = member->order;
        item.depth_key = 0.0f;

        if (member->kind == RL_HANDLE_KIND_MODEL ||
            member->kind == RL_HANDLE_KIND_SPRITE3D ||
            member->kind == RL_HANDLE_KIND_SHAPE ||
            member->kind == RL_HANDLE_KIND_TEXT3D) {
            has_3d = true;
        } else {
            has_2d = true;
            instance->overlay2d[instance->overlay2d_count++] = item;
        }
    }

    if (has_3d) {
        int start = 0;
        rl_render_begin_mode_3d();

        while (start < instance->member_count) {
            int current_layer = 0;
            int end = 0;

            while (start < instance->member_count) {
                rl_scene_member_t *member = &instance->sorted[start];

                if (!is_drawable_visible(member->drawable, member->kind) ||
                    !is_drawable_handle_valid(member->drawable, member->kind) ||
                    (member->kind != RL_HANDLE_KIND_MODEL &&
                     member->kind != RL_HANDLE_KIND_SPRITE3D &&
                     member->kind != RL_HANDLE_KIND_SHAPE &&
                     member->kind != RL_HANDLE_KIND_TEXT3D)) {
                    start++;
                    continue;
                }

                current_layer = member->layer;
                break;
            }

            if (start >= instance->member_count) {
                break;
            }

            instance->opaque3d_count = 0;
            instance->transparent3d_count = 0;
            end = start;

            while (end < instance->member_count &&
                   instance->sorted[end].layer == current_layer) {
                rl_scene_member_t *member = &instance->sorted[end];
                rl_scene_draw_item_t item = {0};

                if (is_drawable_visible(member->drawable, member->kind) &&
                    is_drawable_handle_valid(member->drawable, member->kind) &&
                    (member->kind == RL_HANDLE_KIND_MODEL ||
                     member->kind == RL_HANDLE_KIND_SPRITE3D ||
                     member->kind == RL_HANDLE_KIND_SHAPE ||
                     member->kind == RL_HANDLE_KIND_TEXT3D)) {
                    item.drawable = member->drawable;
                    item.kind = member->kind;
                    item.order = member->order;
                    item.depth_key = 0.0f;

                    if ((member->kind == RL_HANDLE_KIND_MODEL &&
                         rl_model_has_render_pass(member->drawable, RL_RENDER_PASS_OPAQUE_3D)) ||
                        (member->kind == RL_HANDLE_KIND_SPRITE3D &&
                         rl_sprite3d_has_render_pass(member->drawable, RL_RENDER_PASS_OPAQUE_3D)) ||
                        (member->kind == RL_HANDLE_KIND_SHAPE &&
                         rl_shape_has_render_pass(member->drawable, RL_RENDER_PASS_OPAQUE_3D))) {
                        instance->opaque3d[instance->opaque3d_count++] = item;
                    }

                    if ((member->kind == RL_HANDLE_KIND_MODEL &&
                         rl_model_has_render_pass(member->drawable, RL_RENDER_PASS_TRANSPARENT_3D)) ||
                        (member->kind == RL_HANDLE_KIND_SPRITE3D &&
                         rl_sprite3d_has_render_pass(member->drawable, RL_RENDER_PASS_TRANSPARENT_3D)) ||
                        (member->kind == RL_HANDLE_KIND_TEXT3D &&
                         rl_text3d_has_render_pass(member->drawable, RL_RENDER_PASS_TRANSPARENT_3D))) {
                        item.depth_key = compute_transparent_depth_key(camera_data,
                                                                       camera_forward,
                                                                       member->drawable,
                                                                       member->kind);
                        instance->transparent3d[instance->transparent3d_count++] = item;
                    }
                }

                end++;
            }

            for (i = 0; i < instance->opaque3d_count; i++) {
                draw_scene_member_pass(instance->opaque3d[i].drawable,
                                       instance->opaque3d[i].kind,
                                       RL_RENDER_PASS_OPAQUE_3D);
            }

            if (instance->transparent3d_count > 0) {
                sort_transparent_draw_items(instance->transparent3d,
                                            instance->transparent3d_count);
                rlDisableDepthMask();
                for (i = 0; i < instance->transparent3d_count; i++) {
                    draw_scene_member_pass(instance->transparent3d[i].drawable,
                                           instance->transparent3d[i].kind,
                                           RL_RENDER_PASS_TRANSPARENT_3D);
                    /* TODO: This per-item flush preserves mixed Sprite3d/Model transparent order,
                     * but it defeats batching. Replace with a cheaper unified transparent
                     * submission strategy once model/sprite pass behavior settles. */
                    rlDrawRenderBatchActive();
                }
                rlDrawRenderBatchActive();
                rlEnableDepthMask();
            }

            start = end;
        }

        rl_render_end_mode_3d();
    }

    if (has_2d) {
        for (i = 0; i < instance->overlay2d_count; i++) {
            draw_scene_member(instance->overlay2d[i].drawable,
                              instance->overlay2d[i].kind);
        }
    }
}

RL_KEEP
rl_pick_result_t rl_scene_pick(rl_handle_t scene,
                               rl_handle_t camera,
                               float mouse_x,
                               float mouse_y)
{
    rl_scene_instance_t *instance = resolve_scene_instance(scene);
    rl_pick_result_t best = {0};
    rl_handle_t pick_camera = 0;
    Camera3D camera_data = {0};
    Ray ray = {0};
    int i = 0;

    if (instance == NULL) {
        return best;
    }

    pick_camera = resolve_scene_camera_handle(instance, camera);
    if (pick_camera == 0) {
        return best;
    }

    if (!rl_camera3d_get_camera(pick_camera, &camera_data)) {
        return best;
    }

    ray = GetMouseRay((Vector2){mouse_x, mouse_y}, camera_data);

    rl_pick_reset_stats();

    for (i = 0; i < instance->member_count; i++) {
        rl_scene_member_t *member = &instance->members[i];
        rl_pick_result_t result = {0};

        if (!is_kind_narrow_pickable(member->kind)) {
            continue;
        }
        if (!is_drawable_handle_valid(member->drawable, member->kind)) {
            continue;
        }

        if (member->kind == RL_HANDLE_KIND_MODEL) {
            if (!rl_model_is_pickable(member->drawable)) {
                continue;
            }
            if (!rl_model_scene_pick_broadphase(member->drawable, ray)) {
                continue;
            }
            result = rl_pick_model_with_camera_ray(camera_data, ray, member->drawable);
        } else if (member->kind == RL_HANDLE_KIND_SPRITE3D) {
            if (!rl_sprite3d_is_pickable(member->drawable)) {
                continue;
            }
            if (!rl_sprite3d_scene_pick_broadphase(member->drawable, ray)) {
                continue;
            }
            result = rl_pick_sprite3d_with_camera_ray(camera_data, ray, member->drawable);
        } else if (member->kind == RL_HANDLE_KIND_SHAPE) {
            if (!rl_shape_is_pickable(member->drawable)) {
                continue;
            }
            if (!rl_shape_scene_pick_broadphase(member->drawable, ray)) {
                continue;
            }
            result = rl_pick_shape_with_camera_ray(camera_data, ray, member->drawable);
        } else if (member->kind == RL_HANDLE_KIND_TEXT3D) {
            if (!rl_text3d_is_pickable_internal(member->drawable)) {
                continue;
            }
            if (!rl_text3d_scene_pick_broadphase(member->drawable, ray)) {
                continue;
            }
            result = rl_pick_text3d_with_camera_ray(camera_data, ray, member->drawable);
        }

        if (!result.hit) {
            continue;
        }
        if (!best.hit || result.distance < best.distance) {
            best = result;
            best.handle = member->drawable;
        }
    }

    return best;
}

RL_KEEP
bool rl_scene_pick_to_scratch(rl_handle_t scene,
                              rl_handle_t camera,
                              float mouse_x,
                              float mouse_y)
{
    rl_pick_result_t result =
        rl_scene_pick(scene, camera, mouse_x, mouse_y);

    rl_scratch_set_pick_result(result);
    return result.hit;
}

void rl_scene_on_drawable_destroy(rl_handle_t drawable)
{
    int s = 0;
    int index = 0;

    if (drawable == 0) {
        return;
    }

    for (s = 0; s < MAX_SCENES; s++) {
        rl_scene_instance_t *scene = &rl_scenes[s];

        if (!scene->in_use) {
            continue;
        }

        index = find_member_index(scene, drawable);
        if (index < 0) {
            continue;
        }

        scene->member_count--;
        if (index < scene->member_count) {
            scene->members[index] = scene->members[scene->member_count];
        }
    }
}

void rl_scene_init(void)
{
    rl_handle_pool_init(&rl_scene_pool,
                        RL_HANDLE_KIND_SCENE,
                        MAX_SCENES,
                        rl_scene_free_indices,
                        MAX_SCENES,
                        rl_scene_generations,
                        rl_scene_occupied);
    for (int i = 0; i < MAX_SCENES; i++) {
        rl_scenes[i].in_use = false;
        rl_scenes[i].camera = 0;
        rl_scenes[i].member_count = 0;
        rl_scenes[i].next_order = 0;
    }
}

void rl_scene_deinit(void)
{
    int destroyed = 0;

    for (uint16_t i = 1; i < MAX_SCENES; i++) {
        if (!rl_scenes[i].in_use) {
            continue;
        }
        rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_scene_pool, i);
        if (handle == 0) {
            continue;
        }
        rl_scene_destroy(handle);
        destroyed++;
    }
    rl_handle_pool_reset(&rl_scene_pool);
    log_info("rl_scene_deinit: Destroyed %d scenes", destroyed);
}
