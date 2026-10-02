#ifndef WGR_TEXTURE_H
#define WGR_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_resource.h"
#include "wgr_types.h"

/* Texture resource (kind TEXTURE): shared, refcounted, deduped GPU image, loaded on
 * create from an asset path (wgr_resource.h; its status: wgr_resource_get_status).
 * Many Sprite objects may reference one Texture. See docs/ARCHITECTURE.md. */

/* How a texture is sampled where it's used (e.g. per material texture). */
typedef enum {
    WGR_TEXTURE_WRAP_REPEAT = 0, /* tile */
    WGR_TEXTURE_WRAP_CLAMP = 1,  /* stretch the edge texels */
    WGR_TEXTURE_WRAP_MIRROR = 2, /* tile, flipping every other copy */
} wgr_texture_wrap_t;

typedef enum {
    WGR_TEXTURE_FILTER_LINEAR = 0,  /* smooth; blends mipmap levels when minified */
    WGR_TEXTURE_FILTER_NEAREST = 1, /* sharp texels (pixel art) */
} wgr_texture_filter_t;

wgr_handle_t wgr_texture_get_default(void);

/* The texture drawn in place of one that FAILED, and of an image a glTF file
 * references that is missing or broken (the model still loads, with a warning).
 * Default: a built-in magenta and black checker. Set your own (the placeholder holds
 * a reference), or 0 to restore the built-in one. A glTF's missing images take the
 * placeholder set when the model loaded; failed textures draw the current one. */
wgr_handle_t wgr_texture_get_placeholder(void);
bool        wgr_texture_set_placeholder(wgr_handle_t texture);
/* The texture at asset path `path`, loading on create (wgr_resource.h): PENDING at
 * once, then READY, or FAILED in a later frame for a file that is missing, fails to
 * download or won't decode, and FAILED at once for a path that isn't under the asset
 * root (absolute, a drive, or climbing out with "..") or before wgr_run has started
 * the asset layer; each failure is logged with why. 0 only when there's no room for
 * another texture.
 *
 * While it is PENDING it isn't there yet: a sprite or a texture draw using it draws
 * nothing (and a sprite isn't picked), and a material draws as if the slot had no
 * texture. FAILED, it draws as the placeholder (wgr_texture_set_placeholder). Its
 * size reads 0, 0 until it is READY. A texture not loaded from a file (a render
 * target, a model's image) is READY.
 *
 * A path ending .ktx names a texture compressed for GPUs (tools/compress_textures.py):
 * name.bc7.ktx, name.astc.ktx or name.etc2.ktx is loaded, whichever this GPU can
 * sample first, and name.png when none of them can or the variant is missing. On the
 * web only the chosen one downloads. Name the plain "name.ktx"; naming a variant
 * outright loads that one, with no fallback. */
wgr_handle_t wgr_texture_create(const char *path);


/* A texture you can draw into (a render target): width x height pixels, cleared
 * to transparent black each time it's drawn into. Draw into it between
 * wgr_render_begin_texture and wgr_render_end_texture, then use it like any
 * texture. It matches the screen's pixel format and anti-aliasing (MSAA) and has
 * no mipmaps. See docs/HISTORY.md, "Render to texture". */
wgr_handle_t wgr_texture_create_target(int width, int height);

/* How the texture is sampled where it's drawn directly (sprites, wgr_texture_draw).
 * Materials set their own sampling per texture. Default: clamp, linear. Use
 * WGR_TEXTURE_FILTER_NEAREST for crisp scaled-up pixel art. */
bool wgr_texture_set_sampling(wgr_handle_t texture, wgr_texture_wrap_t wrap_u, wgr_texture_wrap_t wrap_v,
                             wgr_texture_filter_t filter);
/* Its size in pixels: 0, 0 until it is READY, and for a handle that isn't a texture. */
vec2_t      wgr_texture_get_size(wgr_handle_t handle);

/* Draw a texture once, axis-aligned, top-left at (x, y) in logical pixels (no
 * object needed). width or height <= 0 uses the texture's size. Draw outside 3D
 * mode; follows call order. For rotation, source regions or picking use sprite2d. */
void        wgr_texture_draw(wgr_handle_t texture, float x, float y, float width, float height,
                            wgr_color_t tint);

/* A region of the texture (source rectangle in texture pixels; width or height <= 0:
 * the whole texture) drawn into the rectangle (x, y, width, height); width or
 * height <= 0 draws it at its own size. Axis-aligned, top-left at (x, y), in call
 * order, like wgr_texture_draw — for icons and panels cut from an atlas. */
void        wgr_texture_draw_ex(wgr_handle_t texture, float source_x, float source_y,
                               float source_width, float source_height,
                               float x, float y, float width, float height, wgr_color_t tint);

/* The same, nine-sliced: borders (left, top, right, bottom) in source pixels keep their
 * size, the edges stretch along one axis and the middle along both — a skinned panel
 * or button at any size. Borders that don't fit shrink to fill it, as with
 * wgr_sprite2d_set_nine_slice; all 0 draws the plain region. */
void        wgr_texture_draw_nine_slice(wgr_handle_t texture, float source_x, float source_y,
                                       float source_width, float source_height,
                                       float left, float top, float right, float bottom,
                                       float x, float y, float width, float height, wgr_color_t tint);

#ifdef __cplusplus
}
#endif

#endif // WGR_TEXTURE_H
