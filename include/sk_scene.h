#ifndef SK_SCENE_H
#define SK_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* A scene is a layered collection of drawable handles plus an active camera.
 * Any handle whose kind has registered a draw function (shapes today; sprites
 * and models later) can be added. sk_scene_draw() activates the scene camera,
 * enters 3D mode, and draws members ordered by layer (ascending). */

sk_handle_t sk_scene_create(void);
void        sk_scene_destroy(sk_handle_t scene);

bool sk_scene_add(sk_handle_t scene, sk_handle_t drawable, int layer);
bool sk_scene_set_layer(sk_handle_t scene, sk_handle_t drawable, int layer);
bool sk_scene_remove(sk_handle_t scene, sk_handle_t drawable);
void sk_scene_clear(sk_handle_t scene);

void sk_scene_set_active_camera(sk_handle_t scene, sk_handle_t camera);

void sk_scene_draw(sk_handle_t scene);

/* Ray-pick the scene at screen pixel (mouse_x, mouse_y) using `camera` (or the
 * scene's active camera if `camera` is 0). Broadphase uses world-space AABBs;
 * narrow phase (when registered) tests the actual shape bounds. Returns the
 * nearest hit. */
sk_pick_result_t sk_scene_pick(sk_handle_t scene, sk_handle_t camera,
                               float mouse_x, float mouse_y);

#ifdef __cplusplus
}
#endif

#endif // SK_SCENE_H
