#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_debug.h"
#include "raylib.h"
#include "internal/rl_color.h"

RL_KEEP
void rl_render_begin() {
    BeginDrawing();
}

RL_KEEP
void rl_render_end() {
    rl_debug_draw();
    EndDrawing();
}

RL_KEEP
void rl_render_clear_background(rl_handle_t color) {
    Color c = rl_color_get(color);
    ClearBackground(c);
}

RL_KEEP
void rl_render_begin_mode_2d(rl_handle_t camera) {
    (void)camera;
}

RL_KEEP
void rl_render_end_mode_2d() {
    EndMode2D();
}

RL_KEEP
void rl_render_begin_mode_3d() {
    Camera3D camera = {0};

    if (!rl_camera3d_ensure_active_camera()) {
        return;
    }
    if (!rl_camera3d_get_active_camera(&camera)) {
        return;
    }

    BeginMode3D(camera);
}

RL_KEEP
void rl_render_end_mode_3d() {
    EndMode3D();
}
