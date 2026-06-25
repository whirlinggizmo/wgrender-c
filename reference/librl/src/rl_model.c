#include "rl_model.h"

#include <math.h>
#include <float.h>
#include <raylib.h>
#include <raymath.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rlgl.h>

#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_color.h"
#include "internal/rl_handle_pool.h"
#include "internal/rl_model.h"
#include "internal/rl_render_pass.h"
#include "internal/rl_scene.h"
#include "path/path.h"

#define MAX_MODELS 255
#define MAX_MODEL_ASSETS 255
#define RL_MODEL_FRAMERATE 60.0f

/*
 * Model system layout:
 * - rl_model_asset_t is shared GPU/animation data loaded from a source file.
 * - rl_model_instance_t is per-handle runtime state (animation time, loop mode, etc).
 * Multiple model handles can point at the same asset and only free it when
 * the last instance releases it.
 */
typedef struct
{
    bool in_use;
    Model *model;
    ModelAnimation *animations;
    int animation_count;
    unsigned int ref_count;
    char source_path[256];
    bool has_source_path;
    BoundingBox local_bounds;
    bool has_local_bounds;
} rl_model_asset_t;

typedef struct
{
    bool in_use;
    rl_handle_t asset_handle;
    float position_x;
    float position_y;
    float position_z;
    float rotation_x;
    float rotation_y;
    float rotation_z;
    float scale_x;
    float scale_y;
    float scale_z;
    int selected_animation;
    float animation_time;
    float animation_speed;
    bool animation_loop;
    bool animation_playing;
    bool animation_gpu_warning_emitted;
    rl_handle_t tint_handle;
    bool visible;
    bool pickable;
} rl_model_instance_t;

static rl_model_asset_t rl_model_assets[MAX_MODEL_ASSETS];
static rl_model_instance_t rl_model_instances[MAX_MODELS];
static rl_handle_pool_t rl_model_asset_pool;
static uint16_t rl_model_asset_free_indices[MAX_MODEL_ASSETS];
static uint16_t rl_model_asset_generations[MAX_MODEL_ASSETS];
static unsigned char rl_model_asset_occupied[MAX_MODEL_ASSETS];
static rl_handle_pool_t rl_model_instance_pool;
static uint16_t rl_model_instance_free_indices[MAX_MODELS];
static uint16_t rl_model_instance_generations[MAX_MODELS];
static unsigned char rl_model_instance_occupied[MAX_MODELS];
static Model rl_model_placeholder;
static rl_handle_t rl_model_default_asset_handle = 0;

static bool is_valid_model(Model model)
{
    return (model.meshCount > 0 && model.meshes != NULL);
}

static void log_invalid_details(const char *filename, Model model)
{
    log_error("Model validity check failed for %s", filename ? filename : "(null)");
    log_error("  model.meshes != NULL: %s", model.meshes ? "true" : "false");
    log_error("  model.materials != NULL: %s", model.materials ? "true" : "false");
    log_error("  model.meshMaterial != NULL: %s", model.meshMaterial ? "true" : "false");
    log_error("  model.meshCount: %d", model.meshCount);
    log_error("  model.materialCount: %d", model.materialCount);
}

static void __attribute__((unused)) log_model_material_details(const char *filename, const Model *model)
{
    if (filename == NULL || model == NULL) {
        return;
    }

    log_debug("Model materials for %s: meshes=%d materials=%d",
             filename, model->meshCount, model->materialCount);

    for (int i = 0; i < model->materialCount; i++) {
        Material material = model->materials[i];
        Texture2D albedo = material.maps[MATERIAL_MAP_ALBEDO].texture;
        Color color = material.maps[MATERIAL_MAP_ALBEDO].color;

        log_debug("  material[%d]: shader=%u albedoTex=%u size=%dx%d format=%d mipmaps=%d color=(%u,%u,%u,%u)",
                 i,
                 material.shader.id,
                 albedo.id,
                 albedo.width,
                 albedo.height,
                 albedo.format,
                 albedo.mipmaps,
                 color.r, color.g, color.b, color.a);

        if ((albedo.id > 0) && (albedo.width == 256) && (albedo.height == 256)) {
            Image gpu_image = LoadImageFromTexture(albedo);
            if (gpu_image.data != NULL) {
                Rectangle gpu_alpha_border = GetImageAlphaBorder(gpu_image, 0.01f);
                log_debug("    gpuReadback: format=%d alphaBorder=(%.0f,%.0f %.0fx%.0f)",
                         gpu_image.format,
                         gpu_alpha_border.x, gpu_alpha_border.y,
                         gpu_alpha_border.width, gpu_alpha_border.height);
                UnloadImage(gpu_image);
            } else {
                log_debug("    gpuReadback: failed");
            }
        }
    }

    if (model->meshMaterial != NULL) {
        for (int i = 0; i < model->meshCount; i++) {
            Mesh mesh = model->meshes[i];
            float min_u = 0.0f;
            float max_u = 0.0f;
            float min_v = 0.0f;
            float max_v = 0.0f;

            if ((mesh.texcoords != NULL) && (mesh.vertexCount > 0)) {
                min_u = max_u = mesh.texcoords[0];
                min_v = max_v = mesh.texcoords[1];

                for (int j = 1; j < mesh.vertexCount; j++) {
                    float u = mesh.texcoords[j*2];
                    float v = mesh.texcoords[j*2 + 1];
                    if (u < min_u) min_u = u;
                    if (u > max_u) max_u = u;
                    if (v < min_v) min_v = v;
                    if (v > max_v) max_v = v;
                }
            }

            log_warn("  mesh[%d] -> material[%d] verts=%d texcoords=%s uvRange=(%.3f,%.3f)-(%.3f,%.3f)",
                     i,
                     model->meshMaterial[i],
                     mesh.vertexCount,
                     mesh.texcoords != NULL ? "yes" : "no",
                     min_u, min_v, max_u, max_v);
        }
    }
}

static void reset_asset(rl_model_asset_t *asset)
{
    if (asset == NULL) {
        return;
    }
    asset->in_use = false;
    asset->model = NULL;
    asset->animations = NULL;
    asset->animation_count = 0;
    asset->ref_count = 0;
    asset->source_path[0] = '\0';
    asset->has_source_path = false;
    asset->local_bounds = (BoundingBox){0};
    asset->has_local_bounds = false;
}

static BoundingBox transform_bounding_box(BoundingBox box, Matrix transform)
{
    Vector3 corners[8] = {
        {box.min.x, box.min.y, box.min.z},
        {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z},
        {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z},
        {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z},
        {box.max.x, box.max.y, box.max.z}
    };
    BoundingBox out = {0};
    out.min = (Vector3){FLT_MAX, FLT_MAX, FLT_MAX};
    out.max = (Vector3){-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (int i = 0; i < 8; i++) {
        Vector3 p = Vector3Transform(corners[i], transform);
        if (p.x < out.min.x) out.min.x = p.x;
        if (p.y < out.min.y) out.min.y = p.y;
        if (p.z < out.min.z) out.min.z = p.z;
        if (p.x > out.max.x) out.max.x = p.x;
        if (p.y > out.max.y) out.max.y = p.y;
        if (p.z > out.max.z) out.max.z = p.z;
    }

    return out;
}

static void reset_instance(rl_model_instance_t *instance)
{
    if (instance == NULL) {
        return;
    }
    instance->in_use = false;
    instance->asset_handle = 0;
    instance->position_x = 0.0f;
    instance->position_y = 0.0f;
    instance->position_z = 0.0f;
    instance->rotation_x = 0.0f;
    instance->rotation_y = 0.0f;
    instance->rotation_z = 0.0f;
    instance->scale_x = 1.0f;
    instance->scale_y = 1.0f;
    instance->scale_z = 1.0f;
    instance->selected_animation = -1;
    instance->animation_time = 0.0f;
    instance->animation_speed = 1.0f;
    instance->animation_loop = true;
    instance->animation_playing = false;
    instance->animation_gpu_warning_emitted = false;
    instance->tint_handle = 0;
    instance->visible = true;
    instance->pickable = true;
}

static bool material_is_transparent(const Material *material)
{
    if (material == NULL) {
        return false;
    }
    return material->maps[MATERIAL_MAP_DIFFUSE].color.a < 255;
}

static bool model_has_mesh_for_pass(const Model *model, rl_render_pass_t pass)
{
    int i = 0;

    if (model == NULL || model->materials == NULL || model->meshMaterial == NULL) {
        return false;
    }

    for (i = 0; i < model->meshCount; i++) {
        int material_index = model->meshMaterial[i];
        bool transparent = false;

        if (material_index < 0 || material_index >= model->materialCount) {
            continue;
        }

        transparent = material_is_transparent(&model->materials[material_index]);
        if ((pass == RL_RENDER_PASS_TRANSPARENT_3D && transparent) ||
            (pass == RL_RENDER_PASS_OPAQUE_3D && !transparent)) {
            return true;
        }
    }

    return false;
}

static void draw_model_placeholder(const rl_model_instance_t *instance)
{
    Quaternion rotation_quat = {0};
    Vector3 rotation_axis = {0.0f, 1.0f, 0.0f};
    float rotation_angle = 0.0f;

    if (instance == NULL) {
        DrawModelEx(rl_model_placeholder,
                    (Vector3){0.0f, 0.0f, 0.0f},
                    (Vector3){0.0f, 1.0f, 0.0f}, 0.0f,
                    (Vector3){1.0f, 1.0f, 1.0f},
                    (Color){255, 0, 255, 255});
        return;
    }

    rotation_quat = QuaternionFromEuler(instance->rotation_x,
                                        instance->rotation_y,
                                        instance->rotation_z);
    QuaternionToAxisAngle(rotation_quat, &rotation_axis, &rotation_angle);
    DrawModelEx(rl_model_placeholder,
                (Vector3){instance->position_x, instance->position_y, instance->position_z},
                rotation_axis, rotation_angle * RAD2DEG,
                (Vector3){instance->scale_x, instance->scale_y, instance->scale_z},
                (Color){255, 0, 255, 255});
}

static rl_model_asset_t *get_asset(rl_handle_t handle)
{
    uint16_t index = 0;

    if (!rl_handle_pool_resolve(&rl_model_asset_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid model asset handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_model_assets[index].in_use) {
        log_warn("Stale model asset handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_model_assets[index];
}

static rl_model_instance_t *get_instance(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_model_instance_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid model handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_model_instances[index].in_use) {
        log_warn("Stale model handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_model_instances[index];
}

static rl_handle_t find_asset_by_path(const char *normalized_path)
{
    if (!normalized_path || normalized_path[0] == '\0') {
        return 0;
    }

    for (uint16_t i = 1; i < MAX_MODEL_ASSETS; i++)
    {
        if (!rl_model_assets[i].in_use || !rl_model_assets[i].has_source_path) {
            continue;
        }
        if (strcmp(rl_model_assets[i].source_path, normalized_path) == 0) {
            return rl_handle_pool_handle_from_index(&rl_model_asset_pool, i);
        }
    }
    return 0;
}

/*
 * Asset lifecycle:
 * - one asset owns loaded model + optional animation clip array
 * - instances retain/release assets and keep per-entity playback state
 */
static bool retain_asset(rl_handle_t asset_handle)
{
    rl_model_asset_t *asset = get_asset(asset_handle);
    if (asset == NULL) return false;
    asset->ref_count++;
    return true;
}

static void release_asset(rl_handle_t asset_handle)
{
    rl_model_asset_t *asset = get_asset(asset_handle);
    if (asset == NULL) {
        return;
    }

    if (asset->ref_count > 0) {
        asset->ref_count--;
    }

    if (asset->ref_count == 0)
    {
        if (asset->animations != NULL && asset->animation_count > 0) {
            UnloadModelAnimations(asset->animations, asset->animation_count);
        }
        if (asset->model != NULL) {
            UnloadModel(*(asset->model));
            free(asset->model);
        }
        reset_asset(asset);
        rl_handle_pool_free(&rl_model_asset_pool, asset_handle);
    }
}

static rl_handle_t create_asset(Model model,
                                         ModelAnimation *animations,
                                         int animation_count,
                                         const char *normalized_path,
                                         bool cache_by_path)
{
    rl_handle_t handle = rl_handle_pool_alloc(&rl_model_asset_pool);
    rl_model_asset_t *asset = NULL;
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_MODEL_ASSETS reached (%d)", MAX_MODEL_ASSETS);
        if (animations != NULL && animation_count > 0) {
            UnloadModelAnimations(animations, animation_count);
        }
        UnloadModel(model);
        return 0;
    }

    rl_handle_pool_resolve(&rl_model_asset_pool, handle, &index);
    asset = &rl_model_assets[index];
    reset_asset(asset);

    asset->model = malloc(sizeof(Model));
    if (asset->model == NULL)
    {
        log_error("Failed to allocate model asset storage");
        rl_handle_pool_free(&rl_model_asset_pool, handle);
        if (animations != NULL && animation_count > 0) {
            UnloadModelAnimations(animations, animation_count);
        }
        UnloadModel(model);
        return 0;
    }

    *(asset->model) = model;
    asset->animations = animations;
    asset->animation_count = animation_count;
    asset->ref_count = 1;
    asset->in_use = true;

    if (cache_by_path && normalized_path != NULL) {
        snprintf(asset->source_path, sizeof(asset->source_path), "%s", normalized_path);
        asset->has_source_path = true;
    }

    asset->local_bounds = GetModelBoundingBox(model);
    asset->has_local_bounds = true;

    return handle;
}

static rl_handle_t create_instance(rl_handle_t asset_handle)
{
    rl_handle_t handle = rl_handle_pool_alloc(&rl_model_instance_pool);
    rl_model_instance_t *instance = NULL;
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_MODELS reached (%d)", MAX_MODELS);
        return 0;
    }
    rl_handle_pool_resolve(&rl_model_instance_pool, handle, &index);

    instance = &rl_model_instances[index];
    reset_instance(instance);
    instance->in_use = true;
    instance->asset_handle = asset_handle;

    if (asset_handle != 0) {
        rl_model_asset_t *asset = get_asset(asset_handle);
        if (asset != NULL && asset->animation_count > 0) {
            instance->selected_animation = 0;
            instance->animation_playing = true;
        }
    }

    return handle;
}

static bool prepare_animation_gpu_state(rl_model_instance_t *instance, Model *model, rl_handle_t handle)
{
    if (instance == NULL || model == NULL || !IsWindowReady()) {
        return false;
    }

    // Animated update requires both CPU anim arrays and GPU VBOs.
    // If normal VBO is missing we can still animate positions; we just disable
    // animated normals once to avoid repeated per-frame warnings.
    for (int m = 0; m < model->meshCount; m++)
    {
        Mesh *mesh = &model->meshes[m];

        if ((mesh->boneWeights == NULL) || (mesh->boneIndices == NULL) ||
            (mesh->animVertices == NULL) || (mesh->animNormals == NULL)) {
            continue;
        }

        if ((mesh->vboId == NULL) || (mesh->vboId[SHADER_LOC_VERTEX_POSITION] == 0))
        {
            if (!instance->animation_gpu_warning_emitted)
            {
                log_warn("Skipping model animation, missing position VBO for handle %u mesh %d",
                        handle, m);
                instance->animation_gpu_warning_emitted = true;
            }
            return false;
        }

        if ((mesh->normals != NULL) && (mesh->vboId[SHADER_LOC_VERTEX_NORMAL] == 0))
        {
            if (!instance->animation_gpu_warning_emitted)
            {
                log_warn("Model animation normals disabled for handle %u mesh %d (missing normal VBO)",
                        handle, m);
                instance->animation_gpu_warning_emitted = true;
            }
            mesh->normals = NULL;
        }
    }

    return true;
}

RL_KEEP
rl_handle_t rl_model_get_default_asset(void)
{
    return rl_model_default_asset_handle;
}

RL_KEEP
rl_handle_t rl_model_load_asset(const char *filename)
{
    char normalized_path[256] = {0};
    rl_handle_t asset_handle = 0;
    Model loaded_model = {0};
    ModelAnimation *animations = NULL;
    int animation_count = 0;
    bool using_placeholder = false;

    if (!filename || filename[0] == '\0') {
        return 0;
    }
    if (!IsWindowReady()) {
        log_error("rl_model_load_asset(%s) called before window/context is ready", filename);
        return 0;
    }

    path_normalize(filename, normalized_path, sizeof(normalized_path));

    asset_handle = find_asset_by_path(normalized_path);
    if (asset_handle != 0) {
        retain_asset(asset_handle);
        return asset_handle;
    }

    loaded_model = LoadModel(normalized_path);
    if (!is_valid_model(loaded_model))
    {
        log_invalid_details(normalized_path, loaded_model);
        log_error("Failed to load model (%s). Substituting placeholder.", normalized_path);
        if (loaded_model.meshCount > 0 || loaded_model.materialCount > 0) {
            UnloadModel(loaded_model);
        }
        loaded_model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
        if (!is_valid_model(loaded_model)) {
            log_error("Failed to create placeholder model for (%s)", normalized_path);
            return 0;
        }
        using_placeholder = true;
    }

    //log_model_material_details(normalized_path, &loaded_model);

    if (!using_placeholder) {
        animations = LoadModelAnimations(normalized_path, &animation_count);
        if (animations == NULL || animation_count <= 0) {
            animations = NULL;
            animation_count = 0;
        }
    }

    return create_asset(loaded_model, animations, animation_count,
                                 normalized_path, true);
}

RL_KEEP
void rl_model_destroy_asset(rl_handle_t asset_handle)
{
    release_asset(asset_handle);
}

RL_KEEP
rl_handle_t rl_model_create(rl_handle_t asset_handle)
{
    rl_handle_t instance_handle = 0;

    if (asset_handle != 0 && !retain_asset(asset_handle)) {
        log_warn("Invalid model asset handle (%u) for rl_model_create", asset_handle);
        return 0;
    }

    instance_handle = create_instance(asset_handle);
    if (instance_handle == 0 && asset_handle != 0) {
        release_asset(asset_handle);
    }
    return instance_handle;
}

RL_KEEP
rl_handle_t rl_model_create_from_file(const char *filename)
{
    rl_handle_t asset_handle = rl_model_load_asset(filename);
    rl_handle_t instance_handle = create_instance(asset_handle);
    if (instance_handle == 0 && asset_handle != 0) {
        release_asset(asset_handle);
    }
    return instance_handle;
}

RL_KEEP
bool rl_model_set_asset(rl_handle_t handle, rl_handle_t asset_handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL) return false;

    if (asset_handle != 0 && !retain_asset(asset_handle)) {
        log_warn("Invalid model asset handle (%u) for rl_model_set_asset", asset_handle);
        return false;
    }
    if (instance->asset_handle != 0) release_asset(instance->asset_handle);

    instance->asset_handle = asset_handle;
    
    // the animation may have been set before we set the asset, honor it
    // instance->selected_animation = -1;
    //instance->animation_time = 0.0f;
    //instance->animation_playing = false;
    instance->animation_gpu_warning_emitted = false;

    // auto play animation 0 (default/idle?) if another animation isn't already set
    if (asset_handle != 0 && instance->selected_animation == -1) {
        rl_model_asset_t *asset = get_asset(asset_handle);
        if (asset != NULL && asset->animation_count > 0) {
            instance->selected_animation = 0;
            instance->animation_playing = true;
        }
    }
    return true;
}

RL_KEEP
void rl_model_destroy(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL) {
        return;
    }

    release_asset(instance->asset_handle);
    reset_instance(instance);
    rl_scene_on_drawable_destroy(handle);
    rl_handle_pool_free(&rl_model_instance_pool, handle);
}

RL_KEEP
int rl_model_get_animation_count(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;

    if (instance == NULL) {
        return 0;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL) {
        return 0;
    }

    return asset->animation_count;
}

RL_KEEP
int rl_model_get_animation_frame_count(rl_handle_t handle, int animation_index)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;

    if (instance == NULL) {
        return 0;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL) {
        return 0;
    }

    if (animation_index < 0 || animation_index >= asset->animation_count) {
        return 0;
    }

    return asset->animations[animation_index].keyframeCount;
}

RL_KEEP
void rl_model_update_animation(rl_handle_t handle, int animation_index, int frame)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;
    int frame_count = 0;
    int normalized_frame = 0;

    if (instance == NULL) {
        return;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL) {
        return;
    }

    if (animation_index < 0 || animation_index >= asset->animation_count) {
        return;
    }

    frame_count = asset->animations[animation_index].keyframeCount;
    if (frame_count <= 0) {
        return;
    }

    if (!prepare_animation_gpu_state(instance, asset->model, handle)) {
        return;
    }

    normalized_frame = frame % frame_count;
    if (normalized_frame < 0) {
        normalized_frame += frame_count;
    }

    UpdateModelAnimation(*(asset->model), asset->animations[animation_index], normalized_frame);
    instance->selected_animation = animation_index;
    instance->animation_time = (float)normalized_frame;
    instance->animation_playing = true;
}

RL_KEEP
bool rl_model_set_animation(rl_handle_t handle, int animation_index)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }

    /*
    // rjk: allow the animation to be set, even if we don't have an asset yet
    rl_model_asset_t *asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->animation_count <= 0) {
        return false;
    }

    if (animation_index < 0 || animation_index >= asset->animation_count) {
        return false;
    }
    */

    instance->selected_animation = animation_index;
    instance->animation_time = 0.0f;
    instance->animation_playing = true;
    return true;
}

RL_KEEP
bool rl_model_set_animation_speed(rl_handle_t handle, float speed)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL) {
        return false;
    }

    instance->animation_speed = speed;
    return true;
}

RL_KEEP
bool rl_model_set_animation_loop(rl_handle_t handle, bool should_loop)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL) {
        return false;
    }

    instance->animation_loop = should_loop;
    return true;
}

RL_KEEP
bool rl_model_set_tint(rl_handle_t handle, rl_handle_t color_handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL) {
        return false;
    }

    instance->tint_handle = color_handle;
    return true;
}

RL_KEEP
bool rl_model_animate(rl_handle_t handle, float delta_seconds)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;
    int frame_count = 0;
    int frame = 0;

    if (instance == NULL) {
        return false;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->animation_count <= 0) {
        return false;
    }

    if (instance->selected_animation < 0 || instance->selected_animation >= asset->animation_count) {
        return false;
    }
    if (!instance->animation_playing) {
        return false;
    }
    if (instance->animation_speed == 0.0f || delta_seconds <= 0.0f) {
        return false;
    }

    frame_count = asset->animations[instance->selected_animation].keyframeCount;
    if (frame_count <= 0) {
        return false;
    }

    if (!prepare_animation_gpu_state(instance, asset->model, handle)) {
        return false;
    }

    instance->animation_time += delta_seconds * (RL_MODEL_FRAMERATE * instance->animation_speed);

    if (instance->animation_loop)
    {
        while (instance->animation_time >= (float)frame_count) {
            instance->animation_time -= (float)frame_count;
        }
        while (instance->animation_time < 0.0f) {
            instance->animation_time += (float)frame_count;
        }
        frame = (int)floorf(instance->animation_time);
    }
    else
    {
        frame = (int)floorf(instance->animation_time);
        if (frame >= frame_count)
        {
            frame = frame_count - 1;
            instance->animation_time = (float)frame;
            instance->animation_playing = false;
        }
        else if (frame < 0)
        {
            frame = 0;
            instance->animation_time = 0.0f;
        }
    }

    UpdateModelAnimation(*(asset->model), asset->animations[instance->selected_animation], frame);
    return true;
}

bool rl_model_get_transform(rl_handle_t handle,
                            float *position_x, float *position_y, float *position_z,
                            float *scale_x, float *scale_y, float *scale_z,
                            float *rotation_x, float *rotation_y, float *rotation_z)
{
    rl_model_instance_t *instance = get_instance(handle);
    if (instance == NULL)
        return false;
    if (position_x) *position_x = instance->position_x;
    if (position_y) *position_y = instance->position_y;
    if (position_z) *position_z = instance->position_z;
    if (scale_x)    *scale_x    = instance->scale_x;
    if (scale_y)    *scale_y    = instance->scale_y;
    if (scale_z)    *scale_z    = instance->scale_z;
    if (rotation_x) *rotation_x = instance->rotation_x;
    if (rotation_y) *rotation_y = instance->rotation_y;
    if (rotation_z) *rotation_z = instance->rotation_z;
    return true;
}

RL_KEEP
bool rl_model_set_transform(rl_handle_t handle,
                            float position_x, float position_y, float position_z,
                            float rotation_x, float rotation_y, float rotation_z,
                            float scale_x, float scale_y, float scale_z)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }

    instance->position_x = position_x;
    instance->position_y = position_y;
    instance->position_z = position_z;
    instance->rotation_x = rotation_x;
    instance->rotation_y = rotation_y;
    instance->rotation_z = rotation_z;
    instance->scale_x = scale_x;
    instance->scale_y = scale_y;
    instance->scale_z = scale_z;
    return true;
}

RL_KEEP
bool rl_model_set_visible(rl_handle_t handle, bool visible)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }
    instance->visible = visible;
    return true;
}

RL_KEEP
bool rl_model_set_pickable(rl_handle_t handle, bool pickable)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }
    instance->pickable = pickable;
    return true;
}

RL_KEEP
bool rl_model_is_visible(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }
    return instance->visible;
}

RL_KEEP
bool rl_model_is_pickable(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);

    if (instance == NULL) {
        return false;
    }
    return instance->pickable;
}

RL_KEEP
bool rl_model_has_render_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;

    if (instance == NULL) {
        return handle != 0 && pass == RL_RENDER_PASS_OPAQUE_3D;
    }

    if (!instance->visible) {
        return false;
    }

    if (instance->asset_handle == 0) {
        return pass == RL_RENDER_PASS_OPAQUE_3D;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        return pass == RL_RENDER_PASS_OPAQUE_3D;
    }

    return model_has_mesh_for_pass(asset->model, pass);
}

RL_KEEP
void rl_model_draw_pass(rl_handle_t handle, rl_render_pass_t pass)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;
    Quaternion rotation_quat = {0};
    Vector3 rotation_axis = {0.0f, 1.0f, 0.0f};
    float rotation_angle = 0.0f;
    Matrix mat_scale = MatrixIdentity();
    Matrix mat_rotation = MatrixIdentity();
    Matrix mat_translation = MatrixIdentity();
    Matrix mat_transform = MatrixIdentity();
    Matrix model_transform = MatrixIdentity();
    Color tint = {255, 255, 255, 255};
    int i = 0;

    if (!rl_model_has_render_pass(handle, pass)) {
        return;
    }

    instance = get_instance(handle);
    if (instance == NULL) {
        if (pass == RL_RENDER_PASS_OPAQUE_3D) {
            draw_model_placeholder(NULL);
        }
        return;
    }

    if (instance->asset_handle == 0) {
        if (pass == RL_RENDER_PASS_OPAQUE_3D) {
            draw_model_placeholder(instance);
        }
        return;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        if (pass == RL_RENDER_PASS_OPAQUE_3D) {
            draw_model_placeholder(instance);
        }
        return;
    }

    rotation_quat = QuaternionFromEuler(instance->rotation_x,
                                        instance->rotation_y,
                                        instance->rotation_z);
    QuaternionToAxisAngle(rotation_quat, &rotation_axis, &rotation_angle);
    mat_scale = MatrixScale(instance->scale_x, instance->scale_y, instance->scale_z);
    mat_rotation = MatrixRotate(rotation_axis, rotation_angle);
    mat_translation = MatrixTranslate(instance->position_x,
                                      instance->position_y,
                                      instance->position_z);
    mat_transform = MatrixMultiply(MatrixMultiply(mat_scale, mat_rotation), mat_translation);
    model_transform = MatrixMultiply(asset->model->transform, mat_transform);
    tint = rl_color_get(instance->tint_handle);

    /* GPU skinning: recompute bone matrices for this instance immediately before
     * drawing so each instance uploads its own pose, not whoever animated last. */
    if (instance->animation_playing &&
        instance->selected_animation >= 0 &&
        instance->selected_animation < asset->animation_count &&
        asset->model->boneMatrices != NULL) {
        int anim_frame_count = asset->animations[instance->selected_animation].keyframeCount;
        if (anim_frame_count > 0) {
            int anim_frame = (int)floorf(instance->animation_time);
            if (anim_frame < 0) anim_frame = 0;
            if (anim_frame >= anim_frame_count) anim_frame = anim_frame_count - 1;
            UpdateModelAnimation(*(asset->model),
                                 asset->animations[instance->selected_animation],
                                 anim_frame);
        }
    }

    for (i = 0; i < asset->model->meshCount; i++) {
        int material_index = asset->model->meshMaterial[i];
        Material material = {0};
        Color diffuse = {0};
        bool transparent = false;

        if (material_index < 0 || material_index >= asset->model->materialCount) {
            continue;
        }

        material = asset->model->materials[material_index];
        transparent = material_is_transparent(&material);
        if ((pass == RL_RENDER_PASS_TRANSPARENT_3D && !transparent) ||
            (pass == RL_RENDER_PASS_OPAQUE_3D && transparent)) {
            continue;
        }

        /* `material` is a shallow copy: material.maps still points at the asset's
         * shared map array, so writing the tinted color mutates the asset. We apply
         * the tint for this draw and restore the original below, otherwise the color
         * would be multiplied by tint/255 every frame and decay toward black. */
        diffuse = material.maps[MATERIAL_MAP_DIFFUSE].color;
        material.maps[MATERIAL_MAP_DIFFUSE].color.r = (unsigned char)(((int)diffuse.r * (int)tint.r) / 255);
        material.maps[MATERIAL_MAP_DIFFUSE].color.g = (unsigned char)(((int)diffuse.g * (int)tint.g) / 255);
        material.maps[MATERIAL_MAP_DIFFUSE].color.b = (unsigned char)(((int)diffuse.b * (int)tint.b) / 255);
        material.maps[MATERIAL_MAP_DIFFUSE].color.a = (unsigned char)(((int)diffuse.a * (int)tint.a) / 255);

        Mesh *current_mesh = &asset->model->meshes[i];
        bool is_gpu_skinned = (asset->model->boneMatrices != NULL) &&
                              (current_mesh->boneIndices != NULL) &&
                              (current_mesh->animVertices == NULL);

        // testing: disable the gpu skin shader
        //is_gpu_skinned = false;                      

        Shader lighting_shader = {0};
        bool use_lighting = is_gpu_skinned
            ? rl_camera3d_get_skinned_lighting_shader(&lighting_shader)
            : rl_camera3d_get_lighting_shader(&lighting_shader);

        if (use_lighting) {
            if (is_gpu_skinned &&
                lighting_shader.locs != NULL &&
                lighting_shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS] != -1) {
                rlEnableShader(lighting_shader.id);
                rlSetUniformMatrices(lighting_shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS],
                                     asset->model->boneMatrices,
                                     asset->model->skeleton.boneCount);
            }
            material.shader = lighting_shader;
        } else if (is_gpu_skinned) {
            rlEnableShader(material.shader.id);
            rlSetUniformMatrices(material.shader.locs[SHADER_LOC_MATRIX_BONETRANSFORMS],
                                 asset->model->boneMatrices,
                                 asset->model->skeleton.boneCount);
        }

        DrawMesh(asset->model->meshes[i], material, model_transform);

        /* Restore the shared material's untinted diffuse color (see note above). */
        material.maps[MATERIAL_MAP_DIFFUSE].color = diffuse;
    }
}

RL_KEEP
void rl_model_draw(rl_handle_t handle)
{
    rl_model_draw_pass(handle, RL_RENDER_PASS_OPAQUE_3D);
    rl_model_draw_pass(handle, RL_RENDER_PASS_TRANSPARENT_3D);
}

bool rl_model_get_ray_collision_ex(rl_handle_t handle,
                                   Ray ray,
                                   Matrix transform,
                                   RayCollision *collision,
                                   Matrix *resolved_model_transform,
                                   bool *broadphase_tested,
                                   bool *broadphase_rejected,
                                   bool *narrowphase_ran)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;
    Matrix model_transform = {0};
    BoundingBox world_bounds = {0};
    bool found_hit = false;
    float closest_distance = 0.0f;

    if (collision == NULL) {
        return false;
    }
    if (broadphase_tested != NULL) *broadphase_tested = false;
    if (broadphase_rejected != NULL) *broadphase_rejected = false;
    if (narrowphase_ran != NULL) *narrowphase_ran = false;

    *collision = (RayCollision){0};
    if (instance == NULL) {
        return false;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        return false;
    }

    model_transform = MatrixMultiply(asset->model->transform, transform);
    if (resolved_model_transform != NULL) *resolved_model_transform = model_transform;
    if (asset->has_local_bounds) {
        RayCollision broad_hit = {0};
        if (broadphase_tested != NULL) *broadphase_tested = true;
        world_bounds = transform_bounding_box(asset->local_bounds, model_transform);
        broad_hit = GetRayCollisionBox(ray, world_bounds);
        if (!broad_hit.hit) {
            if (broadphase_rejected != NULL) *broadphase_rejected = true;
            return true;
        }
    }

    if (narrowphase_ran != NULL) *narrowphase_ran = true;
    for (int i = 0; i < asset->model->meshCount; i++) {
        RayCollision mesh_hit = GetRayCollisionMesh(ray, asset->model->meshes[i], model_transform);
        if (!mesh_hit.hit) {
            continue;
        }
        if (!found_hit || mesh_hit.distance < closest_distance) {
            *collision = mesh_hit;
            closest_distance = mesh_hit.distance;
            found_hit = true;
        }
    }

    return true;
}

static Matrix build_model_instance_matrix(const rl_model_instance_t *inst)
{
    Matrix translation = MatrixTranslate(inst->position_x, inst->position_y, inst->position_z);
    Matrix rotation = MatrixRotateXYZ((Vector3){
        inst->rotation_x,
        inst->rotation_y,
        inst->rotation_z
    });
    Matrix scaling = MatrixScale(inst->scale_x, inst->scale_y, inst->scale_z);
    return MatrixMultiply(MatrixMultiply(scaling, rotation), translation);
}

bool rl_model_scene_pick_broadphase(rl_handle_t handle, Ray ray)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;
    Matrix transform = {0};
    Matrix model_transform = {0};
    BoundingBox world_bounds = {0};
    RayCollision broad_hit = {0};

    if (instance == NULL) {
        return false;
    }
    if (!instance->pickable) {
        return false;
    }
    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        return false;
    }

    transform = build_model_instance_matrix(instance);
    model_transform = MatrixMultiply(asset->model->transform, transform);
    if (!asset->has_local_bounds) {
        return true;
    }

    world_bounds = transform_bounding_box(asset->local_bounds, model_transform);
    broad_hit = GetRayCollisionBox(ray, world_bounds);
    return broad_hit.hit;
}

bool rl_model_get_ray_collision(rl_handle_t handle, Ray ray, Matrix transform, RayCollision *collision)
{
    return rl_model_get_ray_collision_ex(handle, ray, transform, collision, NULL, NULL, NULL, NULL);
}

RL_KEEP
bool rl_model_is_valid(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;

    if (instance == NULL) {
        return false;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        return false;
    }

    return is_valid_model(*(asset->model));
}

RL_KEEP
bool rl_model_is_valid_strict(rl_handle_t handle)
{
    rl_model_instance_t *instance = get_instance(handle);
    rl_model_asset_t *asset = NULL;

    if (instance == NULL) {
        return false;
    }

    asset = get_asset(instance->asset_handle);
    if (asset == NULL || asset->model == NULL) {
        return false;
    }

    return IsModelValid(*(asset->model));
}

void rl_model_init(void)
{
    rl_handle_pool_init(&rl_model_instance_pool,
                        RL_HANDLE_KIND_MODEL,
                        MAX_MODELS,
                        rl_model_instance_free_indices,
                        MAX_MODELS,
                        rl_model_instance_generations,
                        rl_model_instance_occupied);
    rl_handle_pool_init(&rl_model_asset_pool,
                        RL_HANDLE_KIND_MODEL_ASSET,
                        MAX_MODEL_ASSETS,
                        rl_model_asset_free_indices,
                        MAX_MODEL_ASSETS,
                        rl_model_asset_generations,
                        rl_model_asset_occupied);
    for (int i = 0; i < MAX_MODEL_ASSETS; i++) {
        reset_asset(&rl_model_assets[i]);
    }

    for (int i = 0; i < MAX_MODELS; i++) {
        reset_instance(&rl_model_instances[i]);
    }
    rl_model_placeholder = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));

    {
        Model default_model = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
        Model *default_model_ptr = (Model*)malloc(sizeof(Model));
        rl_handle_t h = rl_handle_pool_alloc(&rl_model_asset_pool);
        if (default_model_ptr != NULL && h != 0) {
            uint16_t idx = 0;
            *default_model_ptr = default_model;
            rl_handle_pool_resolve(&rl_model_asset_pool, h, &idx);
            rl_model_assets[idx].model = default_model_ptr;
            rl_model_assets[idx].ref_count = 1;
            rl_model_assets[idx].in_use = true;
            rl_model_assets[idx].animations = NULL;
            rl_model_assets[idx].animation_count = 0;
            rl_model_assets[idx].has_source_path = false;
            rl_model_assets[idx].has_local_bounds = false;
            rl_model_default_asset_handle = h;
        } else {
            if (default_model_ptr != NULL) {
                free(default_model_ptr);
            } else {
                UnloadModel(default_model);
            }
            if (h != 0) rl_handle_pool_free(&rl_model_asset_pool, h);
            log_error("rl_model_init: failed to register default asset handle");
        }
    }
}

void rl_model_deinit(void)
{
    int assets_freed = 0;
    int instances_destroyed = 0;

    for (uint16_t i = 1; i < MAX_MODELS; i++) {
        if (!rl_model_instances[i].in_use) {
            continue;
        }
        {
            rl_handle_t handle = rl_handle_pool_handle_from_index(&rl_model_instance_pool, i);
            if (handle != 0) {
                rl_model_destroy(handle);
                instances_destroyed++;
            }
        }
    }

    for (uint16_t i = 1; i < MAX_MODEL_ASSETS; i++) {
        rl_handle_t asset_handle = rl_handle_pool_handle_from_index(&rl_model_asset_pool, i);
        if (asset_handle != 0) {
            release_asset(asset_handle);
            assets_freed++;
        }
    }

    rl_handle_pool_reset(&rl_model_asset_pool);
    rl_handle_pool_reset(&rl_model_instance_pool);
    UnloadModel(rl_model_placeholder);
    rl_model_placeholder = (Model){0};

    log_info("rl_model_deinit: Destroyed %d model instances", instances_destroyed);
    log_info("rl_model_deinit: Freed %d unused model assets", assets_freed);
}
