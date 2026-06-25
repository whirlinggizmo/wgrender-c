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
 * if the texture was not created pickable (no mask). `*out_alpha` is in [0,1]. */
bool sk_texture_sample_alpha(sk_handle_t handle, float u, float v, float *out_alpha);

#endif // SK_INTERNAL_TEXTURE_H
