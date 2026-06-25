#ifndef SK_INTERNAL_FONT_H
#define SK_INTERNAL_FONT_H

#include "fontstash.h"
#include "sk_types.h"

void sk_font_init(void);
void sk_font_deinit(void);
void sk_font_flush(void); /* upload the atlas (must run outside a render pass) */

/* Shared fontstash context, or NULL if unavailable. */
FONScontext *sk_font_context(void);

/* fontstash font id for a handle, or FONS_INVALID. */
int sk_font_fons_id(sk_handle_t handle);

#endif // SK_INTERNAL_FONT_H
