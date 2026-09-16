#ifndef SK_TEXTURE_H
#define SK_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Texture resource (kind TEXTURE): shared, refcounted, deduped GPU image loaded
 * from a source asset (path). Many Sprite objects may reference one Texture.
 * See docs/ARCHITECTURE.md. */

sk_handle_t sk_texture_get_default(void);
sk_handle_t sk_texture_create(const char *path);
vec2_t      sk_texture_get_size(sk_handle_t handle);

/* Draw a texture once, axis-aligned, top-left at (x, y) in logical pixels (no
 * object needed). width or height <= 0 uses the texture's size. Draw outside 3D
 * mode; follows call order. For rotation, source regions or picking use sprite2d. */
void        sk_texture_draw(sk_handle_t texture, float x, float y, float width, float height,
                            sk_handle_t tint);
void        sk_texture_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXTURE_H
