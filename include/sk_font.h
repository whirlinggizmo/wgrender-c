#ifndef SK_FONT_H
#define SK_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* TrueType fonts via fontstash. A font handle wraps a rasterizable typeface;
 * the pixel size is chosen per draw call (sk_text_draw_ex). Load font bytes
 * however you like (e.g. sk_asset_load_async) and pass them here. */

sk_handle_t sk_font_create_from_memory(const unsigned char *data, int size);
void        sk_font_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_FONT_H
