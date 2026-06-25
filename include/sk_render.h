#ifndef SK_RENDER_H
#define SK_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

void sk_render_begin(void);
void sk_render_end(void);
void sk_render_clear_background(sk_handle_t color);
void sk_render_begin_mode_2d(sk_handle_t camera);
void sk_render_end_mode_2d(void);
void sk_render_begin_mode_3d(void);
void sk_render_end_mode_3d(void);

#ifdef __cplusplus
}
#endif

#endif // SK_RENDER_H
