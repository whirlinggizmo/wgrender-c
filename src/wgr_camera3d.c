#include "wgr_camera3d.h"

#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_scene_internal.h"
#include "wgr_logger.h"

#define CAMERAS_INITIAL 16 /* slots to start with; the pool doubles as needed */
#define WGR_CAMERA3D_BUILTIN_COUNT 1
#define WGR_CAMERA3D_DYNAMIC_START_INDEX (WGR_CAMERA3D_BUILTIN_COUNT + 1)

static wgr_camera3d_t *wgr_cameras; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_camera_pool;
static wgr_handle_t wgr_active_camera = 0;
static unsigned wgr_camera_revision; /* bumped by every change to a camera or the active one */

/* Built-in default camera (index 1, generation 1). */
const wgr_handle_t WGR_CAMERA3D_DEFAULT = WGR_HANDLE_MAKE(WGR_HANDLE_KIND_CAMERA3D, 1, 1);

static bool resolve(wgr_handle_t handle, uint16_t *index_out)
{
    if (!wgr_handle_pool_resolve(&wgr_camera_pool, handle, index_out)) {
        if (handle != 0) {
            log_warn("Invalid camera3d handle (%u)", (unsigned int)handle);
        }
        return false;
    }
    return true;
}

static const wgr_camera3d_t CAMERA_DEFAULTS = {
    .position = {0.0f, 0.0f, 10.0f},
    .target = {0.0f, 0.0f, 0.0f},
    .up = {0.0f, 1.0f, 0.0f},
    .fov = 0.785398163f, /* pi / 4 */
    .ortho_height = 10.0f,
    .projection = WGR_CAMERA3D_PERSPECTIVE,
};

static wgr_camera3d_t *lookup(wgr_handle_t camera)
{
    uint16_t index = 0;
    return resolve(camera, &index) ? &wgr_cameras[index] : NULL;
}

WGR_KEEP
wgr_handle_t wgr_camera3d_create(wgr_camera3d_projection_t projection)
{
    wgr_handle_t handle = wgr_handle_pool_alloc(&wgr_camera_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("camera3d: pool full (%u)", (unsigned)wgr_camera_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_camera_pool, handle, &index);
    wgr_cameras[index] = CAMERA_DEFAULTS;
    wgr_cameras[index].projection =
        projection == WGR_CAMERA3D_ORTHOGRAPHIC ? WGR_CAMERA3D_ORTHOGRAPHIC : WGR_CAMERA3D_PERSPECTIVE;
    return handle;
}

WGR_KEEP
wgr_handle_t wgr_camera3d_get_default(void)
{
    return WGR_CAMERA3D_DEFAULT;
}

WGR_KEEP
bool wgr_camera3d_set_view(wgr_handle_t camera,
                          float position_x, float position_y, float position_z,
                          float target_x, float target_y, float target_z,
                          float up_x, float up_y, float up_z)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL) {
        return false;
    }
    camera_ptr->position = (vec3_t){position_x, position_y, position_z};
    camera_ptr->target = (vec3_t){target_x, target_y, target_z};
    camera_ptr->up = (vec3_t){up_x, up_y, up_z};
    wgr_camera_revision++;
    return true;
}

WGR_KEEP
bool wgr_camera3d_set_projection(wgr_handle_t camera, wgr_camera3d_projection_t projection)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL ||
        (projection != WGR_CAMERA3D_PERSPECTIVE && projection != WGR_CAMERA3D_ORTHOGRAPHIC)) {
        return false;
    }
    camera_ptr->projection = projection;
    wgr_camera_revision++;
    return true;
}

WGR_KEEP
wgr_camera3d_projection_t wgr_camera3d_get_projection(wgr_handle_t camera)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->projection : WGR_CAMERA3D_PERSPECTIVE;
}

WGR_KEEP
bool wgr_camera3d_set_fov(wgr_handle_t camera, float fov)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL || !(fov > 0.0f && fov < 3.14159f)) {
        return false;
    }
    camera_ptr->fov = fov;
    wgr_camera_revision++;
    return true;
}

WGR_KEEP
float wgr_camera3d_get_fov(wgr_handle_t camera)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->fov : 0.0f;
}

WGR_KEEP
bool wgr_camera3d_set_ortho_height(wgr_handle_t camera, float height)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    if (camera_ptr == NULL || !(height > 0.0f)) {
        return false;
    }
    camera_ptr->ortho_height = height;
    wgr_camera_revision++;
    return true;
}

WGR_KEEP
float wgr_camera3d_get_ortho_height(wgr_handle_t camera)
{
    wgr_camera3d_t *camera_ptr = lookup(camera);
    return camera_ptr != NULL ? camera_ptr->ortho_height : 0.0f;
}

WGR_KEEP
bool wgr_camera3d_set_active(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return false;
    }
    wgr_active_camera = handle;
    wgr_camera_revision++;
    return true;
}

WGR_KEEP
wgr_handle_t wgr_camera3d_get_active(void)
{
    return wgr_active_camera;
}

WGR_KEEP
void wgr_camera3d_destroy(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return;
    }
    if (index <= WGR_CAMERA3D_BUILTIN_COUNT) {
        log_error("Cannot destroy built-in camera handle (%u)", (unsigned int)handle);
        return;
    }
    if (wgr_active_camera == handle) {
        wgr_active_camera = WGR_CAMERA3D_DEFAULT;
    }
    wgr_scene_forget(handle); /* scenes using it fall back to the active camera */
    wgr_cameras[index] = (wgr_camera3d_t){0};
    wgr_camera_revision++;
    wgr_handle_pool_free(&wgr_camera_pool, handle);
}

bool wgr_camera3d_ensure_active(void)
{
    uint16_t index = 0;
    if (wgr_active_camera != 0 && resolve(wgr_active_camera, &index)) {
        return true;
    }
    wgr_active_camera = WGR_CAMERA3D_DEFAULT;
    wgr_camera_revision++;
    return resolve(wgr_active_camera, &index);
}

wgr_mat4_t wgr_camera3d_projection(const wgr_camera3d_t *cam, float aspect)
{
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }
    if (cam->projection == WGR_CAMERA3D_ORTHOGRAPHIC) {
        const float top = cam->ortho_height * 0.5f;
        const float right = top * aspect;
        return wgr_mat4_ortho(-right, right, -top, top, WGR_CAMERA3D_ORTHOGRAPHIC_NEAR,
                             WGR_CAMERA3D_ORTHOGRAPHIC_FAR);
    }
    return wgr_mat4_perspective(cam->fov, aspect, WGR_CAMERA3D_PERSPECTIVE_NEAR,
                               WGR_CAMERA3D_PERSPECTIVE_FAR);
}

wgr_mat4_t wgr_camera3d_view(const wgr_camera3d_t *cam)
{
    return wgr_mat4_lookat(cam->position, cam->target, cam->up);
}

bool wgr_camera3d_get_data(wgr_handle_t camera, wgr_camera3d_t *out)
{
    uint16_t index = 0;
    if (camera == 0) {
        return wgr_camera3d_get_active_data(out);
    }
    if (out == NULL || !resolve(camera, &index)) {
        return false;
    }
    *out = wgr_cameras[index];
    return true;
}

bool wgr_camera3d_get_active_data(wgr_camera3d_t *out)
{
    uint16_t index = 0;
    if (out == NULL) {
        return false;
    }
    if (!wgr_camera3d_ensure_active()) {
        return false;
    }
    if (!resolve(wgr_active_camera, &index)) {
        return false;
    }
    *out = wgr_cameras[index];
    return true;
}

void wgr_camera3d_init(void)
{
    if (!wgr_handle_pool_init(&wgr_camera_pool, WGR_HANDLE_KIND_CAMERA3D, "camera3d", (void **)&wgr_cameras,
                             sizeof(wgr_camera3d_t), CAMERAS_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("camera3d: out of memory");
    }

    /* reserve the built-in default slot */
    wgr_camera_pool.generations[1] = 1;
    wgr_camera_pool.occupied[1] = 1;
    wgr_camera_pool.next_index = WGR_CAMERA3D_DYNAMIC_START_INDEX;

    wgr_cameras[1] = CAMERA_DEFAULTS;
    wgr_cameras[1].position = (vec3_t){10.0f, 10.0f, 10.0f};
    wgr_active_camera = WGR_CAMERA3D_DEFAULT;
    wgr_camera_revision++;
}

void wgr_camera3d_deinit(void)
{
    wgr_handle_pool_destroy(&wgr_camera_pool);
    wgr_active_camera = 0;
    wgr_camera_revision++;
}

unsigned wgr_camera3d_revision(void)
{
    return wgr_camera_revision;
}
