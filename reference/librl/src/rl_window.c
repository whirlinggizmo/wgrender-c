#include "internal/exports.h"
#include "rl_window.h"
#include "raylib.h"
#include "rl_scratch.h"

void rl_window_open_internal(int width, int height, const char *title, unsigned int flags) {
    if (IsWindowReady()) {
        return;
    }
    /* Always call before InitWindow. On some targets (e.g. Emscripten) skipping this when
     * flags==0 can leave the GL canvas/context in a bad state; SetConfigFlags(0) is valid. */
    SetConfigFlags((unsigned int)flags);
    InitWindow(width, height, title);
}

RL_KEEP
void rl_window_set_title(const char *title) {
    SetWindowTitle(title);
}

RL_KEEP
void rl_window_set_size(int width, int height) {
    SetWindowSize(width, height);
}

RL_KEEP
int rl_window_close_requested() {
#if defined(PLATFORM_WEB)
    return 0;
#else
    return WindowShouldClose() ? 1 : 0;
#endif
}

vec2_t rl_window_get_screen_size() {
    return (vec2_t){(float)GetScreenWidth(), (float)GetScreenHeight()};
}

RL_KEEP
void rl_window_get_screen_size_to_scratch() {
    vec2_t size = rl_window_get_screen_size();
    rl_scratch_set_vector2(size.x, size.y);
}

RL_KEEP
int rl_window_get_monitor_count() {
#if defined(PLATFORM_DESKTOP)
    return GetMonitorCount();
#else
    return 1;
#endif
}

RL_KEEP
int rl_window_get_current_monitor() {
#if defined(PLATFORM_DESKTOP)
    return GetCurrentMonitor();
#else
    return 0;
#endif
}

RL_KEEP
void rl_window_set_monitor(int monitor) {
#if defined(PLATFORM_DESKTOP)
    SetWindowMonitor(monitor);
#else
    (void)monitor;
#endif
}

RL_KEEP
int rl_window_get_monitor_width(int monitor) {
#if defined(PLATFORM_DESKTOP)
    return GetMonitorWidth(monitor);
#else
    (void)monitor;
    return GetScreenWidth();
#endif
}

RL_KEEP
int rl_window_get_monitor_height(int monitor) {
#if defined(PLATFORM_DESKTOP)
    return GetMonitorHeight(monitor);
#else
    (void)monitor;
    return GetScreenHeight();
#endif
}

vec2_t rl_window_get_monitor_position(int monitor) {
#if defined(PLATFORM_DESKTOP)
    const Vector2 pos = GetMonitorPosition(monitor);
    return (vec2_t){pos.x, pos.y};
#else
    (void)monitor;
    return (vec2_t){0.0f, 0.0f};
#endif
}

RL_KEEP
void rl_window_get_monitor_position_to_scratch(int monitor) {
    vec2_t pos = rl_window_get_monitor_position(monitor);
    rl_scratch_set_vector2(pos.x, pos.y);
}

vec2_t rl_window_get_position() {
    const Vector2 pos = GetWindowPosition();
    return (vec2_t){pos.x, pos.y};
}

RL_KEEP
void rl_window_get_position_to_scratch() {
    vec2_t pos = rl_window_get_position();
    rl_scratch_set_vector2(pos.x, pos.y);
}

RL_KEEP
void rl_window_set_position(int x, int y) {
    SetWindowPosition(x, y);
}

void rl_window_close_internal(void) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
