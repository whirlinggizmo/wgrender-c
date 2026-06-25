#ifndef SK_DEBUG_H
#define SK_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

void sk_debug_enable_fps(int x, int y, int font_size);
void sk_debug_disable_fps(void);

#ifdef __cplusplus
}
#endif

#endif // SK_DEBUG_H
