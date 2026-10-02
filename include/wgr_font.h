#ifndef WGR_FONT_H
#define WGR_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_resource.h"
#include "wgr_types.h"

/* Font resource (kind FONT): a TrueType typeface (fontstash), loaded on create and
 * released like any resource (wgr_resource.h). The pixel size is chosen per draw
 * call (wgr_text_draw_ex), not at create time. Text objects and the default font
 * (wgr_text_set_default_font) hold their own references, so a font stays loaded while
 * anything uses it. See docs/ARCHITECTURE.md. */

/* The font at asset path `path`, loading on create (wgr_resource.h): PENDING at once,
 * then READY, or FAILED in a later frame for a file that is missing, fails to download
 * or isn't a font fontstash takes, and FAILED at once for a path outside the asset
 * root or before wgr_run has started the asset layer. 0 only when there's no room for
 * another font. Text using it draws in the built-in font until it is READY, and stays
 * in it if it FAILED. */
wgr_handle_t wgr_font_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif // WGR_FONT_H
