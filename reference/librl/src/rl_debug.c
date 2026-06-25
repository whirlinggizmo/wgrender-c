#include "rl_debug.h"

#include "internal/rl_debug.h"
#include "rl_color.h"
#include "rl_text.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct rl_debug_state_t {
    bool fps_enabled;
    int fps_x;
    int fps_y;
    int fps_font_size;
    rl_handle_t fps_font;
} rl_debug_state_t;

static rl_debug_state_t rl_debug_state = {0};

void rl_debug_enable_fps(int x, int y, int font_size, rl_handle_t font) {
    rl_debug_state.fps_enabled = true;
    rl_debug_state.fps_x = x;
    rl_debug_state.fps_y = y;
    rl_debug_state.fps_font_size = font_size > 0 ? font_size : 16;
    rl_debug_state.fps_font = font;
}

void rl_debug_disable_fps(void) {
    rl_debug_state.fps_enabled = false;
    rl_debug_state.fps_font = 0;
}

void rl_debug_init(void) {
    rl_debug_state = (rl_debug_state_t){0};
}

void rl_debug_deinit(void) {
    rl_debug_disable_fps();
}

void rl_debug_draw(void) {
    if (!rl_debug_state.fps_enabled) {
        return;
    }

    if (rl_debug_state.fps_font != 0) {
        rl_text_draw_fps_ex(rl_debug_state.fps_font,
                            rl_debug_state.fps_x,
                            rl_debug_state.fps_y,
                            rl_debug_state.fps_font_size,
                            RL_COLOR_DARKGRAY);
        return;
    }

    rl_text_draw_fps(rl_debug_state.fps_x, rl_debug_state.fps_y);
}
