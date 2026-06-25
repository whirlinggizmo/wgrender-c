#include "sk_debug.h"

#include <stdbool.h>

#include "internal/exports.h"
#include "internal/sk_internal.h"
#include "sk_text.h"

typedef struct {
    bool fps_enabled;
    int fps_x;
    int fps_y;
    int fps_font_size;
} sk_debug_state_t;

static sk_debug_state_t sk_debug_state;

void sk_debug_init(void)
{
    sk_debug_state = (sk_debug_state_t){0};
}

void sk_debug_deinit(void)
{
    sk_debug_state.fps_enabled = false;
}

SK_KEEP
void sk_debug_enable_fps(int x, int y, int font_size)
{
    sk_debug_state.fps_enabled = true;
    sk_debug_state.fps_x = x;
    sk_debug_state.fps_y = y;
    sk_debug_state.fps_font_size = font_size > 0 ? font_size : 16;
}

SK_KEEP
void sk_debug_disable_fps(void)
{
    sk_debug_state.fps_enabled = false;
}

void sk_debug_draw(void)
{
    if (!sk_debug_state.fps_enabled) {
        return;
    }
    sk_text_draw_fps(sk_debug_state.fps_x, sk_debug_state.fps_y);
}
