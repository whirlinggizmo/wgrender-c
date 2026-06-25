#ifndef RL_COLOR_H
#define RL_COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

//#include <raylib.h>
#include "rl_types.h"


extern const rl_handle_t RL_COLOR_DEFAULT;
extern const rl_handle_t RL_COLOR_LIGHTGRAY;
extern const rl_handle_t RL_COLOR_GRAY;
extern const rl_handle_t RL_COLOR_DARKGRAY;
extern const rl_handle_t RL_COLOR_YELLOW;
extern const rl_handle_t RL_COLOR_GOLD;
extern const rl_handle_t RL_COLOR_ORANGE;
extern const rl_handle_t RL_COLOR_PINK;
extern const rl_handle_t RL_COLOR_RED;
extern const rl_handle_t RL_COLOR_MAROON;
extern const rl_handle_t RL_COLOR_GREEN;
extern const rl_handle_t RL_COLOR_LIME;
extern const rl_handle_t RL_COLOR_DARKGREEN;
extern const rl_handle_t RL_COLOR_SKYBLUE;
extern const rl_handle_t RL_COLOR_BLUE;
extern const rl_handle_t RL_COLOR_DARKBLUE;
extern const rl_handle_t RL_COLOR_PURPLE;
extern const rl_handle_t RL_COLOR_VIOLET;
extern const rl_handle_t RL_COLOR_DARKPURPLE;
extern const rl_handle_t RL_COLOR_BEIGE;
extern const rl_handle_t RL_COLOR_BROWN;
extern const rl_handle_t RL_COLOR_DARKBROWN;
extern const rl_handle_t RL_COLOR_WHITE;
extern const rl_handle_t RL_COLOR_BLACK;
extern const rl_handle_t RL_COLOR_BLANK;
extern const rl_handle_t RL_COLOR_MAGENTA;
extern const rl_handle_t RL_COLOR_RAYWHITE;


rl_handle_t rl_color_create(int r, int g, int b, int a) ;
void rl_color_destroy(rl_handle_t handle) ;

#ifdef __cplusplus
}
#endif

#endif // RL_COLOR_H
