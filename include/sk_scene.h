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

/* Environment lighting (sk_environment.h): lights the scene's PBR models with
 * reflections and diffuse light from the environment, on top of lights and
 * ambient. intensity scales it (1 = as authored); rotation (radians) turns it
 * around the world up (+y) axis. environment 0 removes it. The scene holds its
 * own reference. Default: none. */
bool sk_scene_set_environment(sk_handle_t scene, sk_handle_t environment, float intensity, float rotation);

/* Draw an environment behind everything the scene draws (a skybox), with the
 * scene's environment intensity and rotation when it's the same environment, else
 * intensity 1 and no rotation. blur 0..1: sharp to fully blurred. environment 0
 * removes it. Default: none. */
bool sk_scene_set_background(sk_handle_t scene, sk_handle_t environment, float blur);

/* How the scene's lit colors map to the display. Lighting can exceed what a screen
 * shows; tone mapping rolls off highlights instead of clipping them. Applies to
 * models and the background, not to sprites, shapes or text. */
typedef enum {
    SK_TONEMAP_NONE = 0,    /* clip */
    SK_TONEMAP_NEUTRAL = 1, /* Khronos PBR Neutral: colors kept until highlights roll off (default) */
    SK_TONEMAP_ACES = 2,    /* filmic: more contrast, highlights shift toward white */
} sk_tonemap_t;

/* exposure in stops (EV): +1 doubles brightness. Default: NEUTRAL, 0. */
bool sk_scene_set_tonemap(sk_handle_t scene, sk_tonemap_t tonemap, float exposure);

void sk_scene_draw(sk_handle_t scene);

/* Pointer interaction (docs/PLAN-2d.md). An interactive scene picks under the pointer
 * (the mouse, or the primary touch) once per frame, before the frame's ticks, against
 * where its members were last drawn, and tracks hover and press per member: 2D
 * members first (topmost), then the nearest 3D member. Only pickable, visible members
 * are hit; a member that isn't enabled (sk_<kind>_set_enabled) is still hit and blocks
 * the pointer, but its hover and press stay UP and it's never clicked.
 *
 * States use the button enum with the same edge rules as keys and buttons: PRESSED and
 * RELEASED are since the previous frame in the frame callback, and since the previous
 * tick in a tick callback. A press that starts on a 2D member captures the pointer
 * (sk_input_is_pointer_captured). Changing interactive resets the scene's state.
 * Default: not interactive. */
bool sk_scene_set_interactive(sk_handle_t scene, bool interactive);
bool sk_scene_is_interactive(sk_handle_t scene);

/* The topmost member under the pointer (enabled or not), or 0. */
sk_handle_t sk_scene_get_hovered(sk_handle_t scene);
/* UP: not under the pointer, PRESSED: came under it, DOWN: under it, RELEASED: left it. */
sk_button_state_t sk_scene_get_hover(sk_handle_t scene, sk_handle_t object);
/* The primary button, for a press that started on this object: PRESSED when it went
 * down, DOWN while held (also when the pointer moved off), RELEASED when let go. */
sk_button_state_t sk_scene_get_press(sk_handle_t scene, sk_handle_t object);
/* Released while still over the object it was pressed on. */
bool sk_scene_is_clicked(sk_handle_t scene, sk_handle_t object);

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
