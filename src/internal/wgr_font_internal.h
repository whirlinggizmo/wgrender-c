#ifndef WGR_INTERNAL_FONT_H
#define WGR_INTERNAL_FONT_H

#include <stdbool.h>

#include "fontstash.h"
#include "sokol_gfx.h"
#include "wgr_font.h"
#include "wgr_types.h"

void wgr_font_init(void);
void wgr_font_deinit(void);
void wgr_font_flush(void); /* upload the atlas (must run outside a render pass) */

/* Shared fontstash context, or NULL if unavailable. */
FONScontext *wgr_font_context(void);

/* The embedded built-in font (src/fonts/wgr_default_font.h), with a reference for
 * the caller; 0 if fontstash isn't available. */
wgr_handle_t wgr_font_create_builtin(void);

/* Reference counting (text objects and the default font hold references). */
void wgr_font_retain(wgr_handle_t handle);

/* fontstash font id for a handle, or FONS_INVALID. */
int wgr_font_fons_id(wgr_handle_t handle);

/* sokol_fontstash's atlas, sampler and glyph shader (false until created). */
bool wgr_fontstash_render_state(FONScontext *context, sg_view *atlas, sg_sampler *sampler, sg_shader *shader);

#endif // WGR_INTERNAL_FONT_H
