#ifndef RL_TEXT_H
#define RL_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

void rl_text_draw_fps(int x, int y);
void rl_text_draw_fps_ex(rl_handle_t font, int x, int y, float fontSize, rl_handle_t color);
void rl_text_draw(const char *text, int x, int y, int fontSize, rl_handle_t color);
void rl_text_draw_ex(rl_handle_t font, const char *text, int x, int y, float fontSize, float spacing, rl_handle_t color);
int rl_text_measure(const char *text, int fontSize);
vec2_t rl_text_measure_ex(rl_handle_t font, const char *text, float fontSize, float spacing);

#ifdef __cplusplus
}
#endif

#endif // RL_TEXT_H
