#include "sk_camera3d.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_scene.h"
#include "sk_logger.h"

#define CAMERAS_INITIAL 16 /* slots to start with; the pool doubles as needed */
#define SK_CAMERA3D_BUILTIN_COUNT 1
#define SK_CAMERA3D_DYNAMIC_START_INDEX (SK_CAMERA3D_BUILTIN_COUNT + 1)

static sk_camera3d_t *sk_cameras; /* grown by the pool: don't hold a pointer across a create */
static sk_handle_pool_t sk_camera_pool;
static sk_handle_t sk_active_camera = 0;
static unsigned sk_camera_revision; /* bumped by every change to a camera or the active one */

/* Built-in default camera (index 1, generation 1). */
const sk_handle_t SK_CAMERA3D_DEFAULT = SK_HANDLE_MAKE(SK_HANDLE_KIND_CAMERA3D, 1, 1);

static bool resolve(sk_handle_t handle, uint16_t *index_out)
{
    if (!sk_handle_pool_resolve(&sk_camera_pool, handle, index_out)) {
        if (handle != 0) {
            log_warn("Invalid camera3d handle (%u)", (unsigned int)handle);
        }
        return false;
    }
    return true;
}

static const sk_camera3d_t CAMERA_DEFAULTS = {
    .position = {0.0f, 0.0f, 10.0f},
    .target = {0.0f, 0.0f, 0.0f},
    .up = {0.0f, 1.0f, 0.0f},
    .fov = 0.785398163f, /* pi / 4 */
    .ortho_height = 10.0f,
    .projection = SK_CAMERA3D_PERSPECTIVE,
};

static sk_camera3d_t *lookup(sk_handle_t camera)
{
    uint16_t index = 0;
    return resolve(camera, &index) ? &sk_cameras[index] : NULL;
}

SK_KEEP
sk_handle_t sk_camera3d_create(sk_camera3d_projection_t projection)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_camera_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("camera3d: pool full (%u)", (unsigned)sk_camera_pool.max - 1u);
        return 0;
    }
    sk_handle_pool_resolve(&sk_camera_pool, handle, &index);
    sk_cameras[index] = CAMERA_DEFAULTS;
    sk_cameras[index].projection =
        projection == SK_CAMERA3D_ORTHOGRAPHIC ? SK_CAMERA3D_ORTHOGRAPHIC : SK_CAMERA3D_PERSPECTIVE;
    return handle;
}

SK_KEEP
sk_handle_t sk_camera3d_get_default(void)
{
    return SK_CAMERA3D_DEFAULT;
}

SK_KEEP
bool sk_camera3d_set_view(sk_handle_t camera,
                          float position_x, float position_y, float position_z,
                          float target_x, float target_y, float target_z,
                          float up_x, float up_y, float up_z)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL) {
        return false;
    }
    camera_ptr->position = (vec3_t){position_x, position_y, position_z};
    camera_ptr->target = (vec3_t){target_x, target_y, target_z};
    camera_ptr->up = (vec3_t){up_x, up_y, up_z};
    sk_camera_revision++;
    return true;
}

SK_KEEP
bool sk_camera3d_set_projection(sk_handle_t camera, sk_camera3d_projection_t projection)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL ||
        (projection != SK_CAMERA3D_PERSPECTIVE && projection != SK_CAMERA3D_ORTHOGRAPHIC)) {
        return false;
    }
    camera_ptr->projection = projection;
    sk_camera_revision++;
    return true;
}

SK_KEEP
sk_camera3d_projection_t sk_camera3d_get_projection(sk_handle_t camera)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->projection : SK_CAMERA3D_PERSPECTIVE;
}

SK_KEEP
bool sk_camera3d_set_fov(sk_handle_t camera, float fov)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL || !(fov > 0.0f && fov < 3.14159f)) {
        return false;
    }
    camera_ptr->fov = fov;
    sk_camera_revision++;
    return true;
}

SK_KEEP
float sk_camera3d_get_fov(sk_handle_t camera)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->fov : 0.0f;
}

SK_KEEP
bool sk_camera3d_set_ortho_height(sk_handle_t camera, float height)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL || !(height > 0.0f)) {
        return false;
    }
    camera_ptr->ortho_height = height;
    sk_camera_revision++;
    return true;
}

SK_KEEP
float sk_camera3d_get_ortho_height(sk_handle_t camera)
{
    sk_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->ortho_height : 0.0f;
}

SK_KEEP
bool sk_camera3d_set_active(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return false;
    }
    sk_active_camera = handle;
    sk_camera_revision++;
    return true;
}

SK_KEEP
sk_handle_t sk_camera3d_get_active(void)
{
    return sk_active_camera;
}

SK_KEEP
void sk_camera3d_destroy(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return;
    }
    if (index <= SK_CAMERA3D_BUILTIN_COUNT) {
        log_error("Cannot destroy built-in camera handle (%u)", (unsigned int)handle);
        return;
    }
    if (sk_active_camera == handle) {
        sk_active_camera = SK_CAMERA3D_DEFAULT;
    }
    sk_scene_forget(handle); /* scenes using it fall back to the active camera */
    sk_cameras[index] = (sk_camera3d_t){0};
    sk_camera_revision++;
    sk_handle_pool_free(&sk_camera_pool, handle);
}

bool sk_camera3d_ensure_active(void)
{
    uint16_t index = 0;
    if (sk_active_camera != 0 && resolve(sk_active_camera, &index)) {
        return true;
    }
    sk_active_camera = SK_CAMERA3D_DEFAULT;
    sk_camera_revision++;
    return resolve(sk_active_camera, &index);
}

sk_mat4_t sk_camera3d_projection(const sk_camera3d_t *cam, float aspect)
{
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }
    if (cam->projection == SK_CAMERA3D_ORTHOGRAPHIC) {
        const float top = cam->ortho_height * 0.5f;
        const float right = top * aspect;
        return sk_mat4_ortho(-right, right, -top, top, SK_CAMERA3D_ORTHOGRAPHIC_NEAR,
                             SK_CAMERA3D_ORTHOGRAPHIC_FAR);
    }
    return sk_mat4_perspective(cam->fov, aspect, SK_CAMERA3D_PERSPECTIVE_NEAR,
                               SK_CAMERA3D_PERSPECTIVE_FAR);
}

sk_mat4_t sk_camera3d_view(const sk_camera3d_t *cam)
{
    return sk_mat4_lookat(cam->position, cam->target, cam->up);
}

bool sk_camera3d_get_data(sk_handle_t camera, sk_camera3d_t *out)
{
    uint16_t index = 0;
    if (camera == 0) {
        return sk_camera3d_get_active_data(out);
    }
    if (out == NULL || !resolve(camera, &index)) {
        return false;
    }
    *out = sk_cameras[index];
    return true;
}

bool sk_camera3d_get_active_data(sk_camera3d_t *out)
{
    uint16_t index = 0;
    if (out == NULL) {
        return false;
    }
    if (!sk_camera3d_ensure_active()) {
        return false;
    }
    if (!resolve(sk_active_camera, &index)) {
        return false;
    }
    *out = sk_cameras[index];
    return true;
}

void sk_camera3d_init(void)
{
    if (!sk_handle_pool_init(&sk_camera_pool, SK_HANDLE_KIND_CAMERA3D, "camera3d", (void **)&sk_cameras,
                             sizeof(sk_camera3d_t), CAMERAS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("camera3d: out of memory");
    }

    /* reserve the built-in default slot */
    sk_camera_pool.generations[1] = 1;
    sk_camera_pool.occupied[1] = 1;
    sk_camera_pool.next_index = SK_CAMERA3D_DYNAMIC_START_INDEX;

    sk_cameras[1] = CAMERA_DEFAULTS;
    sk_cameras[1].position = (vec3_t){10.0f, 10.0f, 10.0f};
    sk_active_camera = SK_CAMERA3D_DEFAULT;
    sk_camera_revision++;
}

void sk_camera3d_deinit(void)
{
    sk_handle_pool_destroy(&sk_camera_pool);
    sk_active_camera = 0;
    sk_camera_revision++;
}

unsigned sk_camera3d_revision(void)
{
    return sk_camera_revision;
}
