#include "sk_camera3d.h"

#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_handle_pool.h"
#include "sk_logger.h"

#define MAX_CAMERAS 64
#define SK_CAMERA3D_BUILTIN_COUNT 1
#define SK_CAMERA3D_DYNAMIC_START_INDEX (SK_CAMERA3D_BUILTIN_COUNT + 1)

static sk_camera3d_data_t sk_cameras[MAX_CAMERAS];
static sk_handle_pool_t sk_camera_pool;
static uint16_t sk_camera_free_indices[MAX_CAMERAS];
static uint16_t sk_camera_generations[MAX_CAMERAS];
static unsigned char sk_camera_occupied[MAX_CAMERAS];
static sk_handle_t sk_active_camera = 0;

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

static void store(uint16_t index,
                  float px, float py, float pz,
                  float tx, float ty, float tz,
                  float ux, float uy, float uz,
                  float fovy, int projection)
{
    sk_cameras[index] = (sk_camera3d_data_t){
        .position = {px, py, pz},
        .target = {tx, ty, tz},
        .up = {ux, uy, uz},
        .fovy = fovy,
        .projection = projection,
    };
}

SK_KEEP
sk_handle_t sk_camera3d_create(float position_x, float position_y, float position_z,
                               float target_x, float target_y, float target_z,
                               float up_x, float up_y, float up_z,
                               float fovy, int projection)
{
    sk_handle_t handle = sk_handle_pool_alloc(&sk_camera_pool);
    uint16_t index = 0;

    if (handle == 0) {
        log_error("MAX_CAMERAS reached (%d)", MAX_CAMERAS);
        return 0;
    }
    sk_handle_pool_resolve(&sk_camera_pool, handle, &index);
    store(index, position_x, position_y, position_z, target_x, target_y, target_z,
          up_x, up_y, up_z, fovy, projection);
    return handle;
}

SK_KEEP
sk_handle_t sk_camera3d_get_default(void)
{
    return SK_CAMERA3D_DEFAULT;
}

SK_KEEP
bool sk_camera3d_set(sk_handle_t handle,
                     float position_x, float position_y, float position_z,
                     float target_x, float target_y, float target_z,
                     float up_x, float up_y, float up_z,
                     float fovy, int projection)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return false;
    }
    store(index, position_x, position_y, position_z, target_x, target_y, target_z,
          up_x, up_y, up_z, fovy, projection);
    return true;
}

SK_KEEP
bool sk_camera3d_set_active(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!resolve(handle, &index)) {
        return false;
    }
    sk_active_camera = handle;
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
    sk_cameras[index] = (sk_camera3d_data_t){0};
    sk_handle_pool_free(&sk_camera_pool, handle);
}

bool sk_camera3d_ensure_active(void)
{
    uint16_t index = 0;
    if (sk_active_camera != 0 && resolve(sk_active_camera, &index)) {
        return true;
    }
    sk_active_camera = SK_CAMERA3D_DEFAULT;
    return resolve(sk_active_camera, &index);
}

bool sk_camera3d_get_active_data(sk_camera3d_data_t *out)
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
    memset(sk_cameras, 0, sizeof(sk_cameras));
    sk_handle_pool_init(&sk_camera_pool,
                        SK_HANDLE_KIND_CAMERA3D,
                        MAX_CAMERAS,
                        sk_camera_free_indices,
                        MAX_CAMERAS,
                        sk_camera_generations,
                        sk_camera_occupied);

    /* reserve the built-in default slot */
    sk_camera_generations[1] = 1;
    sk_camera_occupied[1] = 1;
    sk_camera_pool.next_index = SK_CAMERA3D_DYNAMIC_START_INDEX;

    store(1, 10.0f, 10.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
          45.0f, SK_CAMERA3D_PERSPECTIVE);
    sk_active_camera = SK_CAMERA3D_DEFAULT;
}

void sk_camera3d_deinit(void)
{
    sk_handle_pool_reset(&sk_camera_pool);
    sk_active_camera = 0;
}
