#ifndef SK_FONT_H
#define SK_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Font resource (kind FONT): a TrueType typeface (fontstash) loaded from a path.
 * The pixel size is chosen per draw call (sk_text_draw_ex), not at create time.
 * See docs/ARCHITECTURE.md. */

sk_handle_t sk_font_create(const char *path);
void        sk_font_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_FONT_H
