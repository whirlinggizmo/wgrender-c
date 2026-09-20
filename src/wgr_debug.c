#include "wgr_debug.h"

#include <stdbool.h>

#include "internal/exports_internal.h"
#include "internal/wgr_internal_internal.h"
#include "wgr_text.h"

typedef struct {
    bool fps_enabled;
    int fps_x;
    int fps_y;
    int fps_font_size;
} wgr_debug_state_t;

static wgr_debug_state_t wgr_debug_state;

void wgri_debug_init(void)
{
    wgr_debug_state = (wgr_debug_state_t){0};
}

void wgri_debug_deinit(void)
{
    wgr_debug_state.fps_enabled = false;
}

WGRI_KEEP
void wgr_debug_enable_fps(int x, int y, int font_size)
{
    wgr_debug_state.fps_enabled = true;
    wgr_debug_state.fps_x = x;
    wgr_debug_state.fps_y = y;
    wgr_debug_state.fps_font_size = font_size > 0 ? font_size : 16;
}

WGRI_KEEP
void wgr_debug_disable_fps(void)
{
    wgr_debug_state.fps_enabled = false;
}

void wgri_debug_draw(void)
{
    if (!wgr_debug_state.fps_enabled) {
        return;
    }
    wgr_text_draw_fps(wgr_debug_state.fps_x, wgr_debug_state.fps_y);
}
