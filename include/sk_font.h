#ifndef SK_FONT_H
#define SK_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Font resource (kind FONT): a TrueType typeface (fontstash) loaded from a path.
 * The pixel size is chosen per draw call (sk_text_draw_ex), not at create time.
 * See docs/ARCHITECTURE.md. */

/* Fonts are resources: sk_font_create returns the existing font for a path with a
 * reference added, and sk_font_destroy drops one reference. Text objects and the
 * default font (sk_text_set_default_font) hold their own references, so a font stays
 * loaded while anything uses it. */
sk_handle_t sk_font_create(const char *path);
void        sk_font_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_FONT_H
