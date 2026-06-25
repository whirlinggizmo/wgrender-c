#include <math.h>
#include <stdbool.h>

#include "raylib.h"
#include "rl.h"
#include "rl_camera3d.h"
//#include "rl_color.h"
#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_handle_pool.h"

// This module owns 3D camera tracking used by sprite3d and all simple lighting state.
// Public API remains in rl.h; rl.c just delegates lifecycle via rl_camera3d_init/deinit.

static bool rl_lighting_enabled = false;
static bool rl_lighting_ready = false;
static Shader rl_lighting_shader = {0};
static int rl_light_dir_loc = -1;
static int rl_light_ambient_loc = -1;
static bool rl_skinned_lighting_ready = false;
static Shader rl_skinned_lighting_shader = {0};
static int rl_skinned_light_dir_loc = -1;
static int rl_skinned_light_ambient_loc = -1;
static Vector3 rl_light_direction = {-0.6f, -1.0f, -0.5f};
static float rl_light_ambient = 0.25f;
static Camera3D rl_active_camera = {0};
static bool rl_has_active_camera = false;
static rl_handle_t rl_active_camera_handle = 0;

#define MAX_CAMERAS 256

typedef struct
{
    bool in_use;
    Camera3D camera;
} rl_camera3d_instance_t;

static rl_camera3d_instance_t rl_cameras[MAX_CAMERAS];
static rl_handle_pool_t rl_camera3d_pool;
static uint16_t rl_camera3d_free_indices[MAX_CAMERAS];
static uint16_t rl_camera3d_generations[MAX_CAMERAS];
static unsigned char rl_camera3d_occupied[MAX_CAMERAS];

const rl_handle_t RL_CAMERA3D_DEFAULT = RL_HANDLE_MAKE(RL_HANDLE_KIND_CAMERA3D, 1u, 1u);

static Camera3D build_camera3d(float position_x, float position_y, float position_z,
                                  float target_x, float target_y, float target_z,
                                  float up_x, float up_y, float up_z,
                                  float fovy, int projection)
{
    Camera3D camera = {0};
    camera.position = (Vector3){position_x, position_y, position_z};
    camera.target = (Vector3){target_x, target_y, target_z};
    camera.up = (Vector3){up_x, up_y, up_z};
    camera.fovy = fovy;
    camera.projection = projection;
    return camera;
}

static rl_camera3d_instance_t *resolve_camera3d_entry(rl_handle_t handle)
{
    uint16_t index = 0;
    if (!rl_handle_pool_resolve(&rl_camera3d_pool, handle, &index)) {
        if (handle != 0) log_warn("Invalid camera3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    if (!rl_cameras[index].in_use) {
        log_warn("Stale camera3d handle (%u)", (unsigned int)handle);
        return NULL;
    }
    return &rl_cameras[index];
}

static void apply_internal_active_camera(Camera3D camera, rl_handle_t handle)
{
    rl_active_camera = camera;
    rl_has_active_camera = true;
    rl_active_camera_handle = handle;
}

static Vector3 normalize_vector3(Vector3 v)
{
    float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length <= 0.00001f) {
        return (Vector3){0.0f, -1.0f, 0.0f};
    }

    float inv_length = 1.0f / length;
    return (Vector3){v.x * inv_length, v.y * inv_length, v.z * inv_length};
}

static bool try_init_lighting(void)
{
    if (rl_lighting_ready) {
        return true;
    }
    if (!IsWindowReady()) {
        return false;
    }

#if defined(PLATFORM_WEB)
    const char *vertex_shader_source =
        "attribute vec3 vertexPosition;\n"
        "attribute vec2 vertexTexCoord;\n"
        "attribute vec3 vertexNormal;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "varying vec2 fragTexCoord;\n"
        "varying vec3 fragNormal;\n"
        "void main() {\n"
        "  fragTexCoord = vertexTexCoord;\n"
        "  fragNormal = normalize(mat3(matModel) * vertexNormal);\n"
        "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
        "}\n";

    const char *fragment_shader_source =
        "precision mediump float;\n"
        "varying vec2 fragTexCoord;\n"
        "varying vec3 fragNormal;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform vec3 lightDirection;\n"
        "uniform float ambient;\n"
        "void main() {\n"
        "  vec4 texColor = texture2D(texture0, fragTexCoord);\n"
        "  vec3 n = normalize(fragNormal);\n"
        "  vec3 l = normalize(-lightDirection);\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  float intensity = clamp(ambient + diffuse, 0.0, 1.0);\n"
        "  gl_FragColor = vec4(texColor.rgb * colDiffuse.rgb * intensity, texColor.a * colDiffuse.a);\n"
        "}\n";
#else
    const char *vertex_shader_source =
        "#version 330\n"
        "in vec3 vertexPosition;\n"
        "in vec2 vertexTexCoord;\n"
        "in vec3 vertexNormal;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "out vec2 fragTexCoord;\n"
        "out vec3 fragNormal;\n"
        "void main() {\n"
        "  fragTexCoord = vertexTexCoord;\n"
        "  fragNormal = normalize(mat3(matModel) * vertexNormal);\n"
        "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
        "}\n";

    const char *fragment_shader_source =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec3 fragNormal;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform vec3 lightDirection;\n"
        "uniform float ambient;\n"
        "out vec4 finalColor;\n"
        "void main() {\n"
        "  vec4 texColor = texture(texture0, fragTexCoord);\n"
        "  vec3 n = normalize(fragNormal);\n"
        "  vec3 l = normalize(-lightDirection);\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  float intensity = clamp(ambient + diffuse, 0.0, 1.0);\n"
        "  finalColor = vec4(texColor.rgb * colDiffuse.rgb * intensity, texColor.a * colDiffuse.a);\n"
        "}\n";
#endif

    rl_lighting_shader = LoadShaderFromMemory(vertex_shader_source, fragment_shader_source);
    if (rl_lighting_shader.id == 0) {
        return false;
    }

    rl_light_dir_loc = GetShaderLocation(rl_lighting_shader, "lightDirection");
    rl_light_ambient_loc = GetShaderLocation(rl_lighting_shader, "ambient");
    rl_lighting_ready = (rl_light_dir_loc >= 0 && rl_light_ambient_loc >= 0);

    if (!rl_lighting_ready) {
        UnloadShader(rl_lighting_shader);
        rl_lighting_shader = (Shader){0};
        rl_light_dir_loc = -1;
        rl_light_ambient_loc = -1;
        return false;
    }

    return true;
}

static bool try_init_skinned_lighting(void)
{
    if (rl_skinned_lighting_ready) {
        return true;
    }
    if (!IsWindowReady()) {
        return false;
    }

#if defined(PLATFORM_WEB)
    const char *vertex_shader_source =
        "#version 100\n"
        "#define MAX_BONE_NUM 64\n"
        "attribute vec3 vertexPosition;\n"
        "attribute vec2 vertexTexCoord;\n"
        "attribute vec3 vertexNormal;\n"
        "attribute vec4 vertexBoneIndices;\n"
        "attribute vec4 vertexBoneWeights;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "uniform mat4 boneMatrices[MAX_BONE_NUM];\n"
        "varying vec2 fragTexCoord;\n"
        "varying vec3 fragNormal;\n"
        /* Extract upper-left 3x3 of transposed bone matrix for normal skinning. */
        /* Matrices are row-major on ES 2.0 (transpose not applied by driver). */
        "mat3 boneRot(mat4 m) {\n"
        "  return mat3(\n"
        "    vec3(m[0].x, m[1].x, m[2].x),\n"
        "    vec3(m[0].y, m[1].y, m[2].y),\n"
        "    vec3(m[0].z, m[1].z, m[2].z));\n"
        "}\n"
        "void main() {\n"
        "  int i0 = int(vertexBoneIndices.x);\n"
        "  int i1 = int(vertexBoneIndices.y);\n"
        "  int i2 = int(vertexBoneIndices.z);\n"
        "  int i3 = int(vertexBoneIndices.w);\n"
        "  mat4 t0 = mat4(\n"
        "    vec4(boneMatrices[i0][0].x, boneMatrices[i0][1].x, boneMatrices[i0][2].x, boneMatrices[i0][3].x),\n"
        "    vec4(boneMatrices[i0][0].y, boneMatrices[i0][1].y, boneMatrices[i0][2].y, boneMatrices[i0][3].y),\n"
        "    vec4(boneMatrices[i0][0].z, boneMatrices[i0][1].z, boneMatrices[i0][2].z, boneMatrices[i0][3].z),\n"
        "    vec4(boneMatrices[i0][0].w, boneMatrices[i0][1].w, boneMatrices[i0][2].w, boneMatrices[i0][3].w));\n"
        "  mat4 t1 = mat4(\n"
        "    vec4(boneMatrices[i1][0].x, boneMatrices[i1][1].x, boneMatrices[i1][2].x, boneMatrices[i1][3].x),\n"
        "    vec4(boneMatrices[i1][0].y, boneMatrices[i1][1].y, boneMatrices[i1][2].y, boneMatrices[i1][3].y),\n"
        "    vec4(boneMatrices[i1][0].z, boneMatrices[i1][1].z, boneMatrices[i1][2].z, boneMatrices[i1][3].z),\n"
        "    vec4(boneMatrices[i1][0].w, boneMatrices[i1][1].w, boneMatrices[i1][2].w, boneMatrices[i1][3].w));\n"
        "  mat4 t2 = mat4(\n"
        "    vec4(boneMatrices[i2][0].x, boneMatrices[i2][1].x, boneMatrices[i2][2].x, boneMatrices[i2][3].x),\n"
        "    vec4(boneMatrices[i2][0].y, boneMatrices[i2][1].y, boneMatrices[i2][2].y, boneMatrices[i2][3].y),\n"
        "    vec4(boneMatrices[i2][0].z, boneMatrices[i2][1].z, boneMatrices[i2][2].z, boneMatrices[i2][3].z),\n"
        "    vec4(boneMatrices[i2][0].w, boneMatrices[i2][1].w, boneMatrices[i2][2].w, boneMatrices[i2][3].w));\n"
        "  mat4 t3 = mat4(\n"
        "    vec4(boneMatrices[i3][0].x, boneMatrices[i3][1].x, boneMatrices[i3][2].x, boneMatrices[i3][3].x),\n"
        "    vec4(boneMatrices[i3][0].y, boneMatrices[i3][1].y, boneMatrices[i3][2].y, boneMatrices[i3][3].y),\n"
        "    vec4(boneMatrices[i3][0].z, boneMatrices[i3][1].z, boneMatrices[i3][2].z, boneMatrices[i3][3].z),\n"
        "    vec4(boneMatrices[i3][0].w, boneMatrices[i3][1].w, boneMatrices[i3][2].w, boneMatrices[i3][3].w));\n"
        "  vec4 skinnedPosition =\n"
        "    vertexBoneWeights.x*(t0*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.y*(t1*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.z*(t2*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.w*(t3*vec4(vertexPosition, 1.0));\n"
        "  vec3 skinnedNormal =\n"
        "    vertexBoneWeights.x*(boneRot(t0)*vertexNormal) +\n"
        "    vertexBoneWeights.y*(boneRot(t1)*vertexNormal) +\n"
        "    vertexBoneWeights.z*(boneRot(t2)*vertexNormal) +\n"
        "    vertexBoneWeights.w*(boneRot(t3)*vertexNormal);\n"
        "  fragTexCoord = vertexTexCoord;\n"
        "  fragNormal = normalize(mat3(matModel) * skinnedNormal);\n"
        "  gl_Position = mvp * skinnedPosition;\n"
        "}\n";

    const char *fragment_shader_source =
        "precision mediump float;\n"
        "varying vec2 fragTexCoord;\n"
        "varying vec3 fragNormal;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform vec3 lightDirection;\n"
        "uniform float ambient;\n"
        "void main() {\n"
        "  vec4 texColor = texture2D(texture0, fragTexCoord);\n"
        "  vec3 n = normalize(fragNormal);\n"
        "  vec3 l = normalize(-lightDirection);\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  float intensity = clamp(ambient + diffuse, 0.0, 1.0);\n"
        "  gl_FragColor = vec4(texColor.rgb * colDiffuse.rgb * intensity, texColor.a * colDiffuse.a);\n"
        "}\n";
#else
    const char *vertex_shader_source =
        "#version 330\n"
        "#define MAX_BONE_NUM 128\n"
        "in vec3 vertexPosition;\n"
        "in vec2 vertexTexCoord;\n"
        "in vec3 vertexNormal;\n"
        "in vec4 vertexBoneIndices;\n"
        "in vec4 vertexBoneWeights;\n"
        "uniform mat4 mvp;\n"
        "uniform mat4 matModel;\n"
        "uniform mat4 boneMatrices[MAX_BONE_NUM];\n"
        "out vec2 fragTexCoord;\n"
        "out vec3 fragNormal;\n"
        "void main() {\n"
        "  int i0 = int(vertexBoneIndices.x);\n"
        "  int i1 = int(vertexBoneIndices.y);\n"
        "  int i2 = int(vertexBoneIndices.z);\n"
        "  int i3 = int(vertexBoneIndices.w);\n"
        "  vec4 skinnedPosition =\n"
        "    vertexBoneWeights.x*(boneMatrices[i0]*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.y*(boneMatrices[i1]*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.z*(boneMatrices[i2]*vec4(vertexPosition, 1.0)) +\n"
        "    vertexBoneWeights.w*(boneMatrices[i3]*vec4(vertexPosition, 1.0));\n"
        "  vec3 skinnedNormal =\n"
        "    vertexBoneWeights.x*(boneMatrices[i0]*vec4(vertexNormal, 0.0)).xyz +\n"
        "    vertexBoneWeights.y*(boneMatrices[i1]*vec4(vertexNormal, 0.0)).xyz +\n"
        "    vertexBoneWeights.z*(boneMatrices[i2]*vec4(vertexNormal, 0.0)).xyz +\n"
        "    vertexBoneWeights.w*(boneMatrices[i3]*vec4(vertexNormal, 0.0)).xyz;\n"
        "  fragTexCoord = vertexTexCoord;\n"
        "  fragNormal = normalize(mat3(matModel) * skinnedNormal);\n"
        "  gl_Position = mvp * skinnedPosition;\n"
        "}\n";

    const char *fragment_shader_source =
        "#version 330\n"
        "in vec2 fragTexCoord;\n"
        "in vec3 fragNormal;\n"
        "uniform sampler2D texture0;\n"
        "uniform vec4 colDiffuse;\n"
        "uniform vec3 lightDirection;\n"
        "uniform float ambient;\n"
        "out vec4 finalColor;\n"
        "void main() {\n"
        "  vec4 texColor = texture(texture0, fragTexCoord);\n"
        "  vec3 n = normalize(fragNormal);\n"
        "  vec3 l = normalize(-lightDirection);\n"
        "  float diffuse = max(dot(n, l), 0.0);\n"
        "  float intensity = clamp(ambient + diffuse, 0.0, 1.0);\n"
        "  finalColor = vec4(texColor.rgb * colDiffuse.rgb * intensity, texColor.a * colDiffuse.a);\n"
        "}\n";
#endif

    rl_skinned_lighting_shader = LoadShaderFromMemory(vertex_shader_source, fragment_shader_source);
    if (rl_skinned_lighting_shader.id == 0) {
        return false;
    }

    rl_skinned_light_dir_loc = GetShaderLocation(rl_skinned_lighting_shader, "lightDirection");
    rl_skinned_light_ambient_loc = GetShaderLocation(rl_skinned_lighting_shader, "ambient");
    rl_skinned_lighting_ready = (rl_skinned_light_dir_loc >= 0 && rl_skinned_light_ambient_loc >= 0);

    if (!rl_skinned_lighting_ready) {
        UnloadShader(rl_skinned_lighting_shader);
        rl_skinned_lighting_shader = (Shader){0};
        rl_skinned_light_dir_loc = -1;
        rl_skinned_light_ambient_loc = -1;
        return false;
    }

    return true;
}

bool rl_camera3d_get_active_camera(Camera3D *camera)
{
    if (!rl_has_active_camera || camera == NULL) {
        return false;
    }
    *camera = rl_active_camera;
    return true;
}

bool rl_camera3d_get_camera(rl_handle_t handle, Camera3D *camera)
{
    rl_camera3d_instance_t *entry = NULL;
    if (camera == NULL) {
        return false;
    }

    entry = resolve_camera3d_entry(handle);
    if (entry == NULL) {
        return false;
    }

    *camera = entry->camera;
    return true;
}

static void clear_active_camera(void)
{
    rl_has_active_camera = false;
    rl_active_camera = (Camera3D){0};
    rl_active_camera_handle = 0;
}

RL_KEEP
rl_handle_t rl_camera3d_create(float position_x, float position_y, float position_z,
                               float target_x, float target_y, float target_z,
                               float up_x, float up_y, float up_z,
                               float fovy, int projection)
{
    rl_handle_t handle = rl_handle_pool_alloc(&rl_camera3d_pool);
    uint16_t index = 0;
    if (handle == 0) {
        log_error("MAX_CAMERAS reached (%d)", MAX_CAMERAS);
        return 0;
    }
    rl_handle_pool_resolve(&rl_camera3d_pool, handle, &index);

    rl_cameras[index].in_use = true;
    rl_cameras[index].camera = build_camera3d(position_x, position_y, position_z,
                                                 target_x, target_y, target_z,
                                                 up_x, up_y, up_z,
                                                 fovy, projection);
    return handle;
}

RL_KEEP
rl_handle_t rl_camera3d_get_default(void)
{
    return RL_CAMERA3D_DEFAULT;
}

RL_KEEP
bool rl_camera3d_set(rl_handle_t handle,
                     float position_x, float position_y, float position_z,
                     float target_x, float target_y, float target_z,
                     float up_x, float up_y, float up_z,
                     float fovy, int projection)
{
    rl_camera3d_instance_t *entry = resolve_camera3d_entry(handle);
    if (entry == NULL) {
        return false;
    }

    entry->camera = build_camera3d(position_x, position_y, position_z,
                                      target_x, target_y, target_z,
                                      up_x, up_y, up_z,
                                      fovy, projection);

    if (rl_active_camera_handle == handle) {
        apply_internal_active_camera(entry->camera, handle);
    }

    return true;
}

RL_KEEP
bool rl_camera3d_set_active(rl_handle_t handle)
{
    rl_camera3d_instance_t *entry = resolve_camera3d_entry(handle);
    if (entry == NULL) {
        return false;
    }

    apply_internal_active_camera(entry->camera, handle);
    return true;
}

RL_KEEP
rl_handle_t rl_camera3d_get_active(void)
{
    return rl_active_camera_handle;
}

RL_KEEP
void rl_camera3d_destroy(rl_handle_t handle)
{
    rl_camera3d_instance_t *entry = resolve_camera3d_entry(handle);
    if (entry == NULL) {
        return;
    }
    if (handle == RL_CAMERA3D_DEFAULT) {
        log_error("Cannot destroy default camera handle (%u)", (unsigned int)handle);
        return;
    }

    if (rl_active_camera_handle == handle) {
        clear_active_camera();
    }

    entry->in_use = false;
    entry->camera = (Camera3D){0};
    rl_handle_pool_free(&rl_camera3d_pool, handle);
}

bool rl_camera3d_ensure_active_camera(void)
{
    if (!rl_has_active_camera)
    {
        rl_camera3d_instance_t *default_entry = resolve_camera3d_entry(RL_CAMERA3D_DEFAULT);
        if (default_entry == NULL) {
            log_error("Missing default camera entry");
            return false;
        }
        apply_internal_active_camera(default_entry->camera, RL_CAMERA3D_DEFAULT);
    }
    return true;
}

RL_KEEP
void rl_enable_lighting(void)
{
    rl_lighting_enabled = true;
    try_init_lighting();
    try_init_skinned_lighting();
}

RL_KEEP
void rl_disable_lighting(void)
{
    rl_lighting_enabled = false;
}

RL_KEEP
int rl_is_lighting_enabled(void)
{
    return rl_lighting_enabled ? 1 : 0;
}

RL_KEEP
void rl_set_light_direction(float x, float y, float z)
{
    rl_light_direction = normalize_vector3((Vector3){x, y, z});
}

RL_KEEP
void rl_set_light_ambient(float ambient)
{
    if (ambient < 0.0f) {
        ambient = 0.0f;
    }
    if (ambient > 1.0f) {
        ambient = 1.0f;
    }
    rl_light_ambient = ambient;
}

bool rl_camera3d_get_lighting_shader(Shader *out)
{
    if (!rl_lighting_enabled || !rl_lighting_ready || out == NULL) {
        return false;
    }
    SetShaderValue(rl_lighting_shader, rl_light_dir_loc,
                   &rl_light_direction, SHADER_UNIFORM_VEC3);
    SetShaderValue(rl_lighting_shader, rl_light_ambient_loc,
                   &rl_light_ambient, SHADER_UNIFORM_FLOAT);
    *out = rl_lighting_shader;
    return true;
}

bool rl_camera3d_get_skinned_lighting_shader(Shader *out)
{
    if (!rl_lighting_enabled || !rl_skinned_lighting_ready || out == NULL) {
        return false;
    }
    SetShaderValue(rl_skinned_lighting_shader, rl_skinned_light_dir_loc,
                   &rl_light_direction, SHADER_UNIFORM_VEC3);
    SetShaderValue(rl_skinned_lighting_shader, rl_skinned_light_ambient_loc,
                   &rl_light_ambient, SHADER_UNIFORM_FLOAT);
    *out = rl_skinned_lighting_shader;
    return true;
}

void rl_camera3d_init(void)
{
    // Reset to deterministic defaults so repeated rl_init/rl_deinit cycles are safe.
    rl_lighting_enabled = false;
    rl_lighting_ready = false;
    rl_lighting_shader = (Shader){0};
    rl_light_dir_loc = -1;
    rl_light_ambient_loc = -1;
    rl_skinned_lighting_ready = false;
    rl_skinned_lighting_shader = (Shader){0};
    rl_skinned_light_dir_loc = -1;
    rl_skinned_light_ambient_loc = -1;
    rl_light_direction = (Vector3){-0.6f, -1.0f, -0.5f};
    rl_light_ambient = 0.25f;
    clear_active_camera();

    rl_handle_pool_init(&rl_camera3d_pool,
                        RL_HANDLE_KIND_CAMERA3D,
                        MAX_CAMERAS,
                        rl_camera3d_free_indices,
                        MAX_CAMERAS,
                        rl_camera3d_generations,
                        rl_camera3d_occupied);

    for (int i = 0; i < MAX_CAMERAS; i++) {
        rl_cameras[i].in_use = false;
        rl_cameras[i].camera = (Camera3D){0};
    }

    {
        rl_handle_t default_handle = rl_handle_pool_alloc(&rl_camera3d_pool);
        uint16_t default_index = 0;
        if (default_handle != RL_CAMERA3D_DEFAULT ||
            !rl_handle_pool_resolve(&rl_camera3d_pool, default_handle, &default_index)) {
            log_error("Failed to initialize default camera handle");
            return;
        }

        rl_cameras[default_index].in_use = true;
        rl_cameras[default_index].camera = build_camera3d(
        4.0f, 4.0f, 4.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        45.0f, CAMERA_PERSPECTIVE);
    }
}

void rl_camera3d_deinit(void)
{
    // Shaders are owned here and must be released before global teardown finishes.
    if (rl_lighting_ready) {
        UnloadShader(rl_lighting_shader);
    }
    if (rl_skinned_lighting_ready) {
        UnloadShader(rl_skinned_lighting_shader);
    }
    rl_lighting_enabled = false;
    rl_lighting_ready = false;
    rl_lighting_shader = (Shader){0};
    rl_light_dir_loc = -1;
    rl_light_ambient_loc = -1;
    rl_skinned_lighting_ready = false;
    rl_skinned_lighting_shader = (Shader){0};
    rl_skinned_light_dir_loc = -1;
    rl_skinned_light_ambient_loc = -1;
    clear_active_camera();

    for (int i = 0; i < MAX_CAMERAS; i++) {
        rl_cameras[i].in_use = false;
        rl_cameras[i].camera = (Camera3D){0};
    }
    rl_handle_pool_reset(&rl_camera3d_pool);
}
