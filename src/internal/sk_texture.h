#ifndef SK_INTERNAL_TEXTURE_H
#define SK_INTERNAL_TEXTURE_H

#include <stdbool.h>

#include "sk_types.h"
#include "sokol_gfx.h"

void sk_texture_init(void);
void sk_texture_deinit(void);

/* Resolve a texture handle to its sokol view + sampler (for sokol_gl drawing).
 * Falls back to the 1x1 white default for handle 0 / invalid handles. */
bool sk_texture_get_binding(sk_handle_t handle, sg_view *view, sg_sampler *smp,
                            int *width, int *height);

/* Sample the retained CPU alpha mask at normalized UV (nearest). Returns false
 * if no mask is available. `*out_alpha` is in [0,1]. */
bool sk_texture_sample_alpha(sk_handle_t handle, float u, float v, float *out_alpha);

/* Reference counting (used by Sprite objects and explicit texture ownership). */
void sk_texture_retain(sk_handle_t handle);
void sk_texture_release(sk_handle_t handle);

/* Build the CPU alpha mask on demand (re-reads from the texture's source path).
 * Returns false if the texture has no path or decode fails. */
bool sk_texture_ensure_alpha_mask(sk_handle_t handle);

/* Create an unnamed texture from RGBA8 pixels (e.g. an image embedded in a glTF
 * file). Returns it with one reference owned by the caller. Keeps an alpha mask
 * when any pixel isn't fully opaque. */
sk_handle_t sk_texture_create_rgba(const unsigned char *rgba, int width, int height);

/* The texture's CPU alpha mask (width x height bytes), built on demand for
 * textures loaded from a path. Returns false when there is none, which means
 * fully opaque for textures created from pixels. */
bool sk_texture_get_alpha_mask(sk_handle_t handle, const unsigned char **alpha, int *width, int *height);

#endif // SK_INTERNAL_TEXTURE_H
