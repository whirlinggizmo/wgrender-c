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
void        sk_texture_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXTURE_H
