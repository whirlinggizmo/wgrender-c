#ifndef SK_LIGHT_H
#define SK_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "sk_types.h"

/* Lights (objects). Angles are radians, like the rest of the libsk API.
 * Add a light to a scene with sk_scene_add(scene, light, 0); a light can be in
 * several scenes. Scenes also have an ambient term
 * (sk_scene_set_ambient). See docs/PLAN-lighting.md.
 *
 * - Nothing is lit implicitly: a new scene has no lights, no ambient and no
 *   environment (sk_scene_set_environment), so its models render black until you
 *   light them. Models drawn outside a scene
 *   (sk_model_draw) are unlit: base color x tint.
 * - Lights affect models only. Shapes and sprites are unlit.
 * - Each model uses up to 8 lights: the ones contributing most to it (brightness,
 *   intensity and falloff at the model's bounds). Point and spot lights whose range
 *   doesn't reach a model are skipped for it.
 * - Parameters follow glTF KHR_lights_punctual, and shading follows glTF
 *   materials (sk_material.h), so lights exported from glTF tools look the same
 *   here. Light colors are sRGB; lighting happens in linear space. A white
 *   directional light with intensity pi (about 3) shows a white, rough, non-metal
 *   surface facing it at full brightness. Point and spot lights fall off with the
 *   inverse square of distance and fade smoothly to zero at `range` (0 = no range
 *   limit).
 * - Setters store values even when they don't apply to the light's type (e.g.
 *   range on a directional light). */

typedef enum {
    SK_LIGHT_DIRECTIONAL = 0, /* direction only, infinitely far (sun, moon) */
    SK_LIGHT_POINT = 1,       /* position + range (lamp, torch) */
    SK_LIGHT_SPOT = 2,        /* position + direction + range + cone (flashlight) */
} sk_light_type_t;

sk_handle_t sk_light_create(sk_light_type_t type);
void        sk_light_destroy(sk_handle_t light);

bool sk_light_set_color(sk_handle_t light, sk_color_t color);   /* default: SK_COLOR_WHITE */
bool sk_light_set_intensity(sk_handle_t light, float intensity);  /* default: 1 */
bool sk_light_set_position(sk_handle_t light, float x, float y, float z);   /* point, spot */
bool sk_light_set_direction(sk_handle_t light, float x, float y, float z);  /* directional, spot; default (0,-1,0) */
bool sk_light_set_range(sk_handle_t light, float range);          /* point, spot; default 0 = unlimited */
bool sk_light_set_spot_cone(sk_handle_t light, float inner_angle, float outer_angle);
                                   /* radians from the spot direction, 0..pi/2 (KHR_lights_punctual
                                    * innerConeAngle / outerConeAngle); default pi/6, pi/4 */
bool sk_light_set_enabled(sk_handle_t light, bool enabled);        /* default: enabled */
bool sk_light_is_enabled(sk_handle_t light);

#ifdef __cplusplus
}
#endif

#endif // SK_LIGHT_H
