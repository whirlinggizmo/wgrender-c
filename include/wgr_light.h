#ifndef WGR_LIGHT_H
#define WGR_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "wgr_types.h"

/* Lights (objects). Angles are radians, like the rest of the libwgrender API.
 * Add a light to a scene with wgr_scene_add(scene, light, 0); a light can be in
 * several scenes. Scenes also have an ambient term
 * (wgr_scene_set_ambient). See docs/PLAN-lighting.md.
 *
 * - Nothing is lit implicitly: a new scene has no lights, no ambient and no
 *   environment (wgr_scene_set_environment), so its models render black until you
 *   light them. Models drawn outside a scene
 *   (wgr_model_draw) are unlit: base color x tint.
 * - Lights affect models only. Shapes and sprites are unlit.
 * - Each model uses up to 8 lights: the ones contributing most to it (brightness,
 *   intensity and falloff at the model's bounds). Point and spot lights whose range
 *   doesn't reach a model are skipped for it.
 * - Parameters follow glTF KHR_lights_punctual, and shading follows glTF
 *   materials (wgr_material.h), so lights exported from glTF tools look the same
 *   here. Light colors are sRGB; lighting happens in linear space. A white
 *   directional light with intensity pi (about 3) shows a white, rough, non-metal
 *   surface facing it at full brightness. Point and spot lights fall off with the
 *   inverse square of distance and fade smoothly to zero at `range` (0 = no range
 *   limit).
 * - Setters store values even when they don't apply to the light's type (e.g.
 *   range on a directional light). */

typedef enum {
    WGR_LIGHT_DIRECTIONAL = 0, /* direction only, infinitely far (sun, moon) */
    WGR_LIGHT_POINT = 1,       /* position + range (lamp, torch) */
    WGR_LIGHT_SPOT = 2,        /* position + direction + range + cone (flashlight) */
} wgr_light_type_t;

wgr_handle_t wgr_light_create(wgr_light_type_t type);
void        wgr_light_destroy(wgr_handle_t light);

bool wgr_light_set_color(wgr_handle_t light, wgr_color_t color);   /* default: WGR_COLOR_WHITE */
bool wgr_light_set_intensity(wgr_handle_t light, float intensity);  /* default: 1 */
bool wgr_light_set_position(wgr_handle_t light, float x, float y, float z);   /* point, spot */
bool wgr_light_set_direction(wgr_handle_t light, float x, float y, float z);  /* directional, spot; default (0,-1,0) */
bool wgr_light_set_range(wgr_handle_t light, float range);          /* point, spot; default 0 = unlimited */
bool wgr_light_set_spot_cone(wgr_handle_t light, float inner_angle, float outer_angle);
                                   /* radians from the spot direction, 0..pi/2 (KHR_lights_punctual
                                    * innerConeAngle / outerConeAngle); default pi/6, pi/4 */
bool wgr_light_set_enabled(wgr_handle_t light, bool enabled);        /* default: enabled */
bool wgr_light_is_enabled(wgr_handle_t light);

/* Shadows (docs/PLAN-shadows.md). A casting light draws what it can see into a depth
 * map once a frame, and surfaces behind something are darkened. Off by default: a map
 * costs a pass and its memory. Directional and spot lights cast; a point light is
 * ignored (warned once) — it would need six maps, one each way. Up to four lights cast
 * at once, in the order the scene finds them; past that a light lights the scene
 * without shadowing it.
 *
 * Models say whether they take part (wgr_model_set_casts_shadow /
 * wgr_model_set_receives_shadow); sprites with a material receive but don't cast. */
bool wgr_light_set_casts_shadows(wgr_handle_t light, bool casts);
bool wgr_light_get_casts_shadows(wgr_handle_t light);

/* How far this light's shadows reach, in world units (default 50). A directional
 * light covers that much of what the camera sees, so less distance is a sharper
 * shadow; a spot light covers its cone out to this or its range, whichever is nearer. */
bool wgr_light_set_shadow_distance(wgr_handle_t light, float distance);

/* Pixels each way of the light's shadow map: 256 to 4096, rounded down to a power of
 * two and clamped to that range (default 2048). Bigger is sharper and slower, and costs
 * 2x the memory each step.
 * The casting lights in a scene share one map, so they all get the largest size any of
 * them asked for: keep them the same unless you mean it. */
bool wgr_light_set_shadow_map_size(wgr_handle_t light, int size);

/* How much of this light a shadow blocks (0..1, default 1 = all of it). Less leaves
 * some of it through, for a softer look that doesn't depend on the scene's ambient. */
bool wgr_light_set_shadow_strength(wgr_handle_t light, float strength);

/* A colour mixed into what a shadow leaves behind (default black: nothing added).
 * Shadows are really coloured by the ambient and environment light that still reaches
 * them — this is the stylised knob for when you want a blue or warm shadow without
 * changing how the rest of the scene is lit. The tint is scaled by how deep the
 * shadow is, so a half-shadowed edge gets half of it. */
bool wgr_light_set_shadow_color(wgr_handle_t light, wgr_color_t color);

/* Depth offsets that keep a surface from shadowing itself, measured in shadow-map
 * texels (what the artifact is made of, so the same numbers hold at any map size or
 * distance): `constant` always, `slope` scaled by how steeply the surface faces the
 * light. Defaults (1, 4). Too little and lit surfaces get a striped "shadow acne"; too
 * much and a shadow creeps away from what casts it, leaving a gap at its feet. */
bool wgr_light_set_shadow_bias(wgr_handle_t light, float constant, float slope);

#ifdef __cplusplus
}
#endif

#endif // WGR_LIGHT_H
