#ifndef WGR_INTERNAL_TEXTURE_H
#define WGR_INTERNAL_TEXTURE_H

#include <stdbool.h>

#include <wgr_texture.h> /* the public header ("" would find this file); wgr_texture_release lives there */
#include "wgr_types.h"
#include "sokol_gfx.h"

void wgr_texture_init(void);
void wgr_texture_deinit(void);

/* Resolve a texture handle to its sokol view + sampler (for sokol_gl drawing).
 * Falls back to the 1x1 white default for handle 0 / invalid handles. */
/* A sampler for these settings (shared, made on first use; freed with the textures):
 * materials' textures (models and sprites) are sampled with these. mipmaps false:
 * the base level only. */
sg_sampler wgr_texture_sampler(wgr_texture_wrap_t wrap_u, wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter,
                              bool mipmaps);

bool wgr_texture_get_binding(wgr_handle_t handle, sg_view *view, sg_sampler *smp,
                            int *width, int *height);

/* Sample the retained CPU alpha mask at normalized UV (nearest). Returns false
 * if no mask is available. `*out_alpha` is in [0,1]. */
bool wgr_texture_sample_alpha(wgr_handle_t handle, float u, float v, float *out_alpha);

/* Reference counting (used by Sprite objects and explicit texture ownership). */
void wgr_texture_retain(wgr_handle_t handle);

/* Build the CPU alpha mask on demand (re-reads from the texture's source path).
 * Returns false if the texture has no path or decode fails. */
bool wgr_texture_ensure_alpha_mask(wgr_handle_t handle);

/* Decoded RGBA8 pixels with a full mipmap chain: the CPU half of loading a texture,
 * safe to build on any thread. */
typedef struct wgr_texture_pixels wgr_texture_pixels_t;

/* Decode a PNG or JPEG file's bytes. NULL on failure (wgr_texture_pixels_error
 * says why, on the same thread). */
wgr_texture_pixels_t *wgr_texture_pixels_decode(const unsigned char *bytes, int size);
const char *wgr_texture_pixels_error(void);
/* A copy of RGBA8 pixels, with mipmaps. */
wgr_texture_pixels_t *wgr_texture_pixels_from_rgba(const unsigned char *rgba, int width, int height);
void wgr_texture_pixels_free(wgr_texture_pixels_t *pixels);

/* Main thread: upload pixels as a texture named `path` (NULL for none), with one
 * reference owned by the caller. `keep_alpha` keeps an alpha mask when any pixel
 * isn't fully opaque (for textures with no file to re-read it from). */
wgr_handle_t wgr_texture_create_pixels(const wgr_texture_pixels_t *pixels, const char *path, bool keep_alpha);

/* Create an unnamed texture from RGBA8 pixels (e.g. an image embedded in a glTF
 * file). Returns it with one reference owned by the caller. Keeps an alpha mask
 * when any pixel isn't fully opaque. */
wgr_handle_t wgr_texture_create_rgba(const unsigned char *rgba, int width, int height);

/* The texture's CPU alpha mask (width x height bytes), built on demand for
 * textures loaded from a path. Returns false when there is none, which means
 * fully opaque for textures created from pixels. */
bool wgr_texture_get_alpha_mask(wgr_handle_t handle, const unsigned char **alpha, int *width, int *height);

/* A render target's pass attachments and size (width and height may be NULL). False
 * when `handle` isn't a target (wgr_texture_create_target). */
bool wgr_texture_get_target(wgr_handle_t handle, sg_attachments *attachments, int *width, int *height);

/* True when the texture's rows are stored bottom-up: render targets on backends
 * whose framebuffer origin is bottom-left (GL, WebGL2). Whoever samples it maps
 * v to 1 - v, so every texture reads top-down. */
bool wgr_texture_is_flipped(wgr_handle_t handle);

/* The target the GPU is drawing into (0 = the screen). wgr_texture_get_binding
 * refuses to bind it, since a pass can't sample its own target. Set by wgr_render
 * while recording and while replaying a target's pass. */
void wgr_texture_set_drawing_into(wgr_handle_t handle);

/* Compressed textures (docs/PLAN-textures.md): the file `path` (textures/rock.ktx)
 * stands for on this GPU, written to `out`: rock.bc7.ktx, rock.astc.ktx, rock.etc2.ktx,
 * else rock.png. A variant name is kept as it is. False for other paths. */
bool wgr_texture_ktx_path(const char *path, char *out, size_t out_size);
/* A texture from a parsed compressed file (a glTF model's, say), with no path: 0 when
 * the GPU can't sample its format. */
struct wgr_ktx_t;
wgr_handle_t wgr_texture_create_ktx(const struct wgr_ktx_t *ktx);
/* For tests: which variants count as usable (bit 0 BC7, 1 ASTC, 2 ETC2); -1 asks the
 * GPU (the default). */
void wgr_texture_set_ktx_support(int mask);

#endif // WGR_INTERNAL_TEXTURE_H
