#ifndef SK_SCENE_H
#define SK_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "sk_types.h"

/* A scene is a layered collection of drawables (models, sprites, shapes) and
 * lights, plus an active camera and an ambient term. sk_scene_draw() activates the
 * scene camera, enters 3D mode and draws each layer (ascending): opaque parts
 * first, then transparent parts sorted back to front. Lights ignore their layer
 * and light the models in the whole scene (see sk_light.h). */

sk_handle_t sk_scene_create(void);
void        sk_scene_destroy(sk_handle_t scene);

bool sk_scene_add(sk_handle_t scene, sk_handle_t drawable, int layer);
bool sk_scene_set_layer(sk_handle_t scene, sk_handle_t drawable, int layer);
bool sk_scene_remove(sk_handle_t scene, sk_handle_t drawable);
void sk_scene_clear(sk_handle_t scene);

void sk_scene_set_active_camera(sk_handle_t scene, sk_handle_t camera);

/* Light added to every lit model in the scene: color x intensity. Default: none
 * (intensity 0). */
bool sk_scene_set_ambient(sk_handle_t scene, sk_handle_t color, float intensity);

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
