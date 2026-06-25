#ifndef RL_WINDOW_H
#define RL_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

#define RL_WINDOW_FLAG_FULLSCREEN_MODE 0x00000002u
#define RL_WINDOW_FLAG_WINDOW_RESIZABLE 0x00000004u
#define RL_WINDOW_FLAG_WINDOW_UNDECORATED 0x00000008u
#define RL_WINDOW_FLAG_WINDOW_TRANSPARENT 0x00000010u
#define RL_WINDOW_FLAG_MSAA_4X_HINT 0x00000020u
#define RL_WINDOW_FLAG_VSYNC_HINT 0x00000040u
#define RL_WINDOW_FLAG_WINDOW_HIDDEN 0x00000080u
#define RL_WINDOW_FLAG_WINDOW_ALWAYS_RUN 0x00000100u
#define RL_WINDOW_FLAG_WINDOW_MINIMIZED 0x00000200u
#define RL_WINDOW_FLAG_WINDOW_MAXIMIZED 0x00000400u
#define RL_WINDOW_FLAG_WINDOW_UNFOCUSED 0x00000800u
#define RL_WINDOW_FLAG_WINDOW_TOPMOST 0x00001000u
#define RL_WINDOW_FLAG_WINDOW_HIGHDPI 0x00002000u
#define RL_WINDOW_FLAG_INTERLACED_HINT 0x00010000u

/*
 * Window lifecycle is owned by core runtime:
 * - rl_init_values(...) opens/configures the window
 * - rl_deinit() closes the window
 *
 * These entry points are intentionally internal and should not be exposed
 * or called by user code:
 *
 * void rl_window_open(int width, int height, const char *title, unsigned int flags);
 * void rl_window_close(void);
 */

void rl_window_set_title(const char *title);
void rl_window_set_size(int width, int height);
int rl_window_close_requested(void);
vec2_t rl_window_get_screen_size(void);
int rl_window_get_monitor_count(void);
int rl_window_get_current_monitor(void);
void rl_window_set_monitor(int monitor);
int rl_window_get_monitor_width(int monitor);
int rl_window_get_monitor_height(int monitor);
vec2_t rl_window_get_monitor_position(int monitor);
vec2_t rl_window_get_position(void);
void rl_window_set_position(int x, int y);

#ifdef __cplusplus
}
#endif

#endif // RL_WINDOW_H
