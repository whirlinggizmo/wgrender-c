#ifndef SK_TEXTURE_H
#define SK_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Texture resource (kind TEXTURE): shared, refcounted, deduped GPU image loaded
 * from a source asset (path). Many Sprite objects may reference one Texture.
 * See docs/ARCHITECTURE.md. */

/* How a texture is sampled where it's used (e.g. per material texture). */
typedef enum {
    SK_TEXTURE_WRAP_REPEAT = 0, /* tile */
    SK_TEXTURE_WRAP_CLAMP = 1,  /* stretch the edge texels */
    SK_TEXTURE_WRAP_MIRROR = 2, /* tile, flipping every other copy */
} sk_texture_wrap_t;

typedef enum {
    SK_TEXTURE_FILTER_LINEAR = 0,  /* smooth; blends mipmap levels when minified */
    SK_TEXTURE_FILTER_NEAREST = 1, /* sharp texels (pixel art) */
} sk_texture_filter_t;

sk_handle_t sk_texture_get_default(void);

/* The texture used in place of one that couldn't be loaded, e.g. an image a glTF
 * file references that is missing or broken (the model still loads, with a
 * warning). Default: a built-in magenta and black checker. Set your own (the
 * placeholder holds a reference), or 0 to restore the built-in one. Affects
 * resources loaded after the call. */
sk_handle_t sk_texture_get_placeholder(void);
bool        sk_texture_set_placeholder(sk_handle_t texture);
sk_handle_t sk_texture_create(const char *path);

/* A texture you can draw into (a render target): width x height pixels, cleared
 * to transparent black each time it's drawn into. Draw into it between
 * sk_render_begin_texture and sk_render_end_texture, then use it like any
 * texture. It matches the screen's pixel format and anti-aliasing (MSAA) and has
 * no mipmaps. See docs/PLAN-render-target.md. */
sk_handle_t sk_texture_create_target(int width, int height);

/* How the texture is sampled where it's drawn directly (sprites, sk_texture_draw).
 * Materials set their own sampling per texture. Default: clamp, linear. Use
 * SK_TEXTURE_FILTER_NEAREST for crisp scaled-up pixel art. */
bool sk_texture_set_sampling(sk_handle_t texture, sk_texture_wrap_t wrap_u, sk_texture_wrap_t wrap_v,
                             sk_texture_filter_t filter);
vec2_t      sk_texture_get_size(sk_handle_t handle);

/* Draw a texture once, axis-aligned, top-left at (x, y) in logical pixels (no
 * object needed). width or height <= 0 uses the texture's size. Draw outside 3D
 * mode; follows call order. For rotation, source regions or picking use sprite2d. */
void        sk_texture_draw(sk_handle_t texture, float x, float y, float width, float height,
                            sk_color_t tint);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        sk_texture_release(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXTURE_H
