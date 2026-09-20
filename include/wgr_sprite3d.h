#ifndef WGR_SPRITE3D_H
#define WGR_SPRITE3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "wgr_types.h"

/* Sprite3d object (kind SPRITE3D): a textured billboard in 3D that references
 * a shared Texture resource. See docs/ARCHITECTURE.md. */

typedef enum {
    WGR_SPRITE3D_FACING_CAMERA = 0,         /* spherical: parallel to the view plane, whatever the camera's pitch */
    WGR_SPRITE3D_FACING_CAMERA_FIXED_Y = 1, /* cylindrical: turns about world Y to face the camera, stays upright */
    WGR_SPRITE3D_FACING_Y_UP = 2,           /* flat in XZ plane, normal +Y   */
    WGR_SPRITE3D_FACING_FREE = 3,           /* its own rotation: the local XY plane, facing +Z */
} wgr_sprite3d_facing_t;

wgr_handle_t wgr_sprite3d_create(wgr_handle_t texture);
bool wgr_sprite3d_set_texture(wgr_handle_t handle, wgr_handle_t texture);
bool wgr_sprite3d_set_transform(wgr_handle_t handle,
                               float position_x, float position_y, float position_z,
                               float rotation_x, float rotation_y, float rotation_z, /* radians */
                               float scale_x, float scale_y, float scale_z);
/* World size of the quad before scale: set_size is the square shorthand for
 * set_extent(size, size). Default 1x1; a width or height <= 0 is refused. */
bool wgr_sprite3d_set_size(wgr_handle_t handle, float size);
bool wgr_sprite3d_set_extent(wgr_handle_t handle, float width, float height);

/* Region of the texture to show, in texture pixels (sprite sheets, atlases).
 * Default: the whole texture; width or height <= 0 resets to that. */
bool wgr_sprite3d_set_source(wgr_handle_t handle, float x, float y, float width, float height);

/* The point of the quad that sits on the sprite's position and that it turns
 * around, as a fraction of the quad: (0, 0) its top-left, (1, 1) its
 * bottom-right, (0.5, 0.5) its center (the default). y runs down the texture, so
 * (0.5, 1) puts the position at the bottom edge — what a sprite standing on the
 * ground wants. */
bool wgr_sprite3d_set_pivot(wgr_handle_t handle, float x, float y);

bool wgr_sprite3d_set_facing(wgr_handle_t handle, wgr_sprite3d_facing_t facing);
vec3_t wgr_sprite3d_get_position(wgr_handle_t handle);
vec3_t wgr_sprite3d_get_rotation(wgr_handle_t handle); /* radians */
vec3_t wgr_sprite3d_get_scale(wgr_handle_t handle);
bool wgr_sprite3d_set_tint(wgr_handle_t handle, wgr_color_t color);
bool wgr_sprite3d_set_visible(wgr_handle_t handle, bool visible);
bool wgr_sprite3d_is_visible(wgr_handle_t handle);
bool wgr_sprite3d_set_pickable(wgr_handle_t handle, bool pickable); /* default: pickable */
bool wgr_sprite3d_is_pickable(wgr_handle_t handle);
/* Enabled (default): a hit reacts (hover, press, click in an interactive scene).
 * Disabled: still drawn and picked, and still blocks the pointer, but doesn't react. */
bool wgr_sprite3d_set_enabled(wgr_handle_t sprite, bool enabled);
bool wgr_sprite3d_is_enabled(wgr_handle_t sprite);
/* How the sprite uses its texture's alpha (default WGR_ALPHA_BLEND). In a scene, blended
 * sprites are sorted back to front with the other transparent parts; opaque and
 * masked sprites (cutoff: texels below it are cut out, 0..1) write depth and aren't
 * sorted, and additive ones are drawn after the blended parts, unsorted. Unsorted
 * sprites are grouped by texture, so they draw in fewer batches. */
bool wgr_sprite3d_set_alpha_mode(wgr_handle_t handle, wgr_alpha_mode_t mode, float cutoff);
wgr_alpha_mode_t wgr_sprite3d_get_alpha_mode(wgr_handle_t handle);
/* Draw the sprite with a material (wgr_material.h); 0 goes back to libwgrender's sprite
 * shader (texture x tint, unlit). The sprite keeps its texture, region, tint, facing
 * and alpha mode, and holds its own reference to the material.
 *
 *   built-in (WGR_MATERIAL_PBR): lit like a model, by the scene's lights and
 *     environment. The sprite's texture is the base color (its tint multiplies it),
 *     the material's factors and its normal, metallic-roughness, occlusion and
 *     emissive maps do the rest, over the sprite's texture region. The quad's facing
 *     is the surface normal, so normal maps work on billboards. Sprites drawn
 *     together (one batch) share the lights chosen for where they are: a sprite far
 *     from the rest of its batch can miss a light near it.
 *   built-in (WGR_MATERIAL_UNLIT): its base color x the sprite's texture and tint.
 *   custom (wgr_material_create_custom): its shader draws the sprite (shaders/wgr.glsl:
 *     wgr_sprite_color() is the sprite's texture times its tint), with the same lights
 *     and environment. */
bool wgr_sprite3d_set_material(wgr_handle_t handle, wgr_handle_t material);
wgr_handle_t wgr_sprite3d_get_material(wgr_handle_t handle); /* borrowed; 0 = none */

/* When enabled, picking ignores hits on texels whose alpha is below `threshold`
 * (0..1). Builds a CPU alpha mask from the texture's source path on demand. */
bool wgr_sprite3d_set_pick_alpha_test(wgr_handle_t handle, bool enable, float threshold);
void wgr_sprite3d_draw(wgr_handle_t handle);
void wgr_sprite3d_destroy(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_SPRITE3D_H
