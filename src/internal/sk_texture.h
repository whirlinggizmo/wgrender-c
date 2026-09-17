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

/* Decoded RGBA8 pixels with a full mipmap chain: the CPU half of loading a texture,
 * safe to build on any thread. */
typedef struct sk_texture_pixels sk_texture_pixels_t;

/* Decode a PNG or JPEG file's bytes. NULL on failure (sk_texture_pixels_error
 * says why, on the same thread). */
sk_texture_pixels_t *sk_texture_pixels_decode(const unsigned char *bytes, int size);
const char *sk_texture_pixels_error(void);
/* A copy of RGBA8 pixels, with mipmaps. */
sk_texture_pixels_t *sk_texture_pixels_from_rgba(const unsigned char *rgba, int width, int height);
void sk_texture_pixels_free(sk_texture_pixels_t *pixels);

/* Main thread: upload pixels as a texture named `path` (NULL for none), with one
 * reference owned by the caller. `keep_alpha` keeps an alpha mask when any pixel
 * isn't fully opaque (for textures with no file to re-read it from). */
sk_handle_t sk_texture_create_pixels(const sk_texture_pixels_t *pixels, const char *path, bool keep_alpha);

/* Create an unnamed texture from RGBA8 pixels (e.g. an image embedded in a glTF
 * file). Returns it with one reference owned by the caller. Keeps an alpha mask
 * when any pixel isn't fully opaque. */
sk_handle_t sk_texture_create_rgba(const unsigned char *rgba, int width, int height);

/* The texture's CPU alpha mask (width x height bytes), built on demand for
 * textures loaded from a path. Returns false when there is none, which means
 * fully opaque for textures created from pixels. */
bool sk_texture_get_alpha_mask(sk_handle_t handle, const unsigned char **alpha, int *width, int *height);

/* A render target's pass attachments and size. False when `handle` isn't a target
 * (sk_texture_create_target). */
bool sk_texture_get_target(sk_handle_t handle, sg_attachments *attachments, int *width, int *height);

/* True when the texture's rows are stored bottom-up: render targets on backends
 * whose framebuffer origin is bottom-left (GL, WebGL2). Whoever samples it maps
 * v to 1 - v, so every texture reads top-down. */
bool sk_texture_is_flipped(sk_handle_t handle);

/* The target the GPU is drawing into (0 = the screen). sk_texture_get_binding
 * refuses to bind it, since a pass can't sample its own target. Set by sk_render
 * while recording and while replaying a target's pass. */
void sk_texture_set_drawing_into(sk_handle_t handle);

#endif // SK_INTERNAL_TEXTURE_H
