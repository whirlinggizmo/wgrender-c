#include "rl_text.h"

#include <raylib.h>

#include "internal/exports.h"
#include "internal/rl_color.h"
#include "internal/rl_font.h"
#include "rl_scratch.h"

RL_KEEP
void rl_text_draw_fps(int x, int y) {
    DrawFPS(x, y);
}

RL_KEEP
void rl_text_draw_fps_ex(rl_handle_t font, int x, int y, float fontSize, rl_handle_t color) {
    Color c = rl_color_get(color);
    Font f = rl_font_get(font);
    int fps = GetFPS();
    Vector2 position = {x, y};

    DrawTextEx(f, TextFormat("%2i FPS", fps), position, fontSize, 1.0, c);
}

RL_KEEP
void rl_text_draw(const char *text, int x, int y, int fontSize, rl_handle_t color) {
    Color c = rl_color_get(color);
    DrawText(text, x, y, fontSize, c);
}

RL_KEEP
void rl_text_draw_ex(rl_handle_t font, const char *text, int x, int y, float fontSize, float spacing, rl_handle_t color) {
    Color c = rl_color_get(color);
    Font f = rl_font_get(font);
    Vector2 position = {x, y};
    DrawTextEx(f, text, position, fontSize, spacing, c);
}

RL_KEEP
int rl_text_measure(const char *text, int fontSize) {
    return MeasureText(text, fontSize);
}

vec2_t rl_text_measure_ex(rl_handle_t font, const char *text, float fontSize, float spacing) {
    Vector2 result = MeasureTextEx(rl_font_get(font), text, fontSize, spacing);
    return (vec2_t){result.x, result.y};
}

RL_KEEP
void rl_text_measure_ex_to_scratch(rl_handle_t font, const char *text, float fontSize, float spacing) {
    vec2_t result = rl_text_measure_ex(font, text, fontSize, spacing);
    rl_scratch_set_vector2(result.x, result.y);
}
