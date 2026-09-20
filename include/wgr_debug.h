#ifndef WGR_DEBUG_H
#define WGR_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

void wgr_debug_enable_fps(int x, int y, int font_size);
void wgr_debug_disable_fps(void);

#ifdef __cplusplus
}
#endif

#endif // WGR_DEBUG_H
