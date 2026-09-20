#ifndef WGRI_INTERNAL_KTX_H
#define WGRI_INTERNAL_KTX_H

#include <stdbool.h>
#include <stddef.h>

#include "sokol_gfx.h"

/* KTX (version 1) files holding GPU-compressed textures (docs/PLAN-textures.md): BC7,
 * ASTC 4x4 or ETC2 RGBA, 16 bytes a 4x4 block, with their mipmaps; what
 * tools/compress_textures.sh writes. Parsing only points into the bytes. */
typedef struct wgri_ktx_t {
    sg_pixel_format format;
    int width, height;
    int mip_count;
    const unsigned char *levels[SG_MAX_MIPMAPS];
    size_t sizes[SG_MAX_MIPMAPS];
} wgri_ktx_t;

/* False (with *error saying why) when the bytes aren't a KTX file libwgrender can use. */
bool wgri_ktx_parse(const unsigned char *bytes, size_t size, wgri_ktx_t *out, const char **error);

#endif // WGRI_INTERNAL_KTX_H
