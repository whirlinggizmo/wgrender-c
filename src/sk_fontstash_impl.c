/* Implementation TU for fontstash + the sokol_fontstash renderer.
 *
 * sokol_gfx/sokol_gl/etc. are implemented in sk_sokol_impl.c; here we only need
 * their declarations. fontstash.h pulls in stb_truetype's implementation. */
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_GLES3) && !defined(SOKOL_D3D11) && \
    !defined(SOKOL_METAL) && !defined(SOKOL_WGPU)
#define SOKOL_GLCORE
#endif

/* fontstash.h's implementation uses libc facilities (FILE, size_t, malloc,
 * memset, math) without including the headers itself. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define SOKOL_FONTSTASH_IMPL
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"

/* declared in internal/sk_font.h (not included: fontstash.h would expand its
 * implementation a second time) */
bool sk_fontstash_render_state(FONScontext *context, sg_view *atlas, sg_sampler *sampler, sg_shader *shader);

/* What sokol_fontstash renders with, for drawing glyph quads with another
 * pipeline (sk_text3d: depth-tested). The atlas view changes when fontstash grows
 * the atlas, so ask again for every draw. */
bool sk_fontstash_render_state(FONScontext *context, sg_view *atlas, sg_sampler *sampler, sg_shader *shader)
{
    const _sfons_t *sfons = context != NULL ? (const _sfons_t *)context->params.userPtr : NULL;
    if (sfons == NULL || sfons->shd.id == SG_INVALID_ID || sfons->tex_view.id == SG_INVALID_ID) {
        return false;
    }
    *atlas = sfons->tex_view;
    *sampler = sfons->smp;
    *shader = sfons->shd;
    return true;
}
