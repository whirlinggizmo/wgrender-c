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

/* Shadows (docs/PLAN-shadows.md). A casting light draws what it can see into a depth
 * map once a frame, and surfaces behind something are darkened. Off by default: a map
 * costs a pass and its memory. Directional lights cast for now; spot and point lights
 * are ignored (warned once). One casting light per scene: the first one found casts.
 *
 * Models say whether they take part (sk_model_set_casts_shadow /
 * sk_model_set_receives_shadow); sprites with a material receive but don't cast. */
bool sk_light_set_casts_shadows(sk_handle_t light, bool casts);
bool sk_light_get_casts_shadows(sk_handle_t light);

/* How far from the camera this light's shadows reach, in world units (default 50).
 * The map covers that much, so less distance is a sharper shadow. */
bool sk_light_set_shadow_distance(sk_handle_t light, float distance);

/* Pixels each way of the light's shadow map: 256 to 4096, rounded down to a power of
 * two (default 2048). Bigger is sharper and slower, and costs 2x the memory each step. */
bool sk_light_set_shadow_map_size(sk_handle_t light, int size);

/* How much of this light a shadow blocks (0..1, default 1 = all of it). Less leaves
 * some of it through, for a softer look that doesn't depend on the scene's ambient. */
bool sk_light_set_shadow_strength(sk_handle_t light, float strength);

/* A colour mixed into what a shadow leaves behind (default black: nothing added).
 * Shadows are really coloured by the ambient and environment light that still reaches
 * them — this is the stylised knob for when you want a blue or warm shadow without
 * changing how the rest of the scene is lit. The tint is scaled by how deep the
 * shadow is, so a half-shadowed edge gets half of it. */
bool sk_light_set_shadow_color(sk_handle_t light, sk_color_t color);

/* Depth offsets that keep a surface from shadowing itself, measured in shadow-map
 * texels (what the artifact is made of, so the same numbers hold at any map size or
 * distance): `constant` always, `slope` scaled by how steeply the surface faces the
 * light. Defaults (1, 4). Too little and lit surfaces get a striped "shadow acne"; too
 * much and a shadow creeps away from what casts it, leaving a gap at its feet. */
bool sk_light_set_shadow_bias(sk_handle_t light, float constant, float slope);

#ifdef __cplusplus
}
#endif

#endif // SK_LIGHT_H
