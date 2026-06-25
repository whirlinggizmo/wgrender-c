#include "rl.h"
#include "internal/exports.h"
#include "internal/rl_camera3d.h"
#include "internal/rl_color.h"
#include "internal/rl_debug.h"
#include "internal/rl_event.h"
#include "internal/rl_font.h"
#include "internal/rl_logger.h"
#include "rl_logger.h"
#include "internal/rl_model.h"
#include "internal/rl_music.h"
#include "internal/rl_sound.h"
#include "internal/rl_scratch.h"
#include "internal/rl_sprite2d.h"
#include "internal/rl_text2d.h"
#include "internal/rl_text3d.h"
#include "internal/rl_scene.h"
#include "internal/rl_sprite3d.h"
#include "internal/rl_shape.h"
#include "internal/rl_texture.h"
#include "internal/rl_window.h"
#include "raylib.h"
#include "rl_fs.h"
#include "rl_asset.h"
#include "rl_version.h"
#include <stddef.h>
#include <string.h>

bool initialized = false;

typedef struct rl_init_config {
    int window_width;
    int window_height;
    const char *window_title;
    unsigned int window_flags;
    const char *asset_host;
    const char *fs_root_dir;
} rl_init_config_t;

static void apply_init_config_defaults(rl_init_config_t *out)
{
    if (out->window_width == 0) {
        out->window_width = 1024;
    }
    if (out->window_height == 0) {
        out->window_height = 1280;
    }
    if (out->window_title == NULL) {
        out->window_title = "librl";
    }
    if (out->fs_root_dir == NULL) {
        out->fs_root_dir = "cache";
    }
}

static int init_runtime_from_config(const rl_init_config_t *config, bool async)
{
    rl_init_config_t cfg;

    if (initialized) {
        return RL_INIT_ERR_ALREADY_INITIALIZED;
    }

    memset(&cfg, 0, sizeof(cfg));
    if (config != NULL) {
        cfg = *config;
    }
    apply_init_config_defaults(&cfg);

    rl_logger_init();
    rl_logger_info("librl %s", rl_version_string());
    if (async) {
        if (rl_fs_init_async(cfg.fs_root_dir) != 0) {
            rl_logger_deinit();
            return RL_INIT_ERR_LOADER;
        }
    } else if (rl_fs_init(cfg.fs_root_dir) != 0) {
        rl_logger_deinit();
        return RL_INIT_ERR_LOADER;
    }
    if (cfg.asset_host != NULL && cfg.asset_host[0] != '\0') {
        if (rl_asset_set_host(cfg.asset_host) != 0) {
            rl_fs_deinit();
            rl_logger_deinit();
            return RL_INIT_ERR_ASSET_HOST;
        }
    }
    rl_window_open_internal(
        cfg.window_width,
        cfg.window_height,
        cfg.window_title,
        cfg.window_flags
    );
    if (!IsWindowReady()) {
        rl_fs_deinit();
        rl_logger_deinit();
        return RL_INIT_ERR_WINDOW;
    }
    rl_scratch_init();
    rl_color_init();
    rl_font_init();
    rl_model_init();
    rl_music_init();
    rl_sound_init();
    rl_event_init();
    rl_camera3d_init();
    rl_texture_init();
    rl_sprite2d_init();
    rl_sprite3d_init();
    rl_text2d_init();
    rl_text3d_init();
    rl_shape_init();
    rl_scene_init();
    rl_debug_init();
    initialized = true;
    return RL_INIT_OK;
}

static rl_init_config_t build_init_config_from_values(int window_width,
                                                   int window_height,
                                                   const char *window_title,
                                                   unsigned int window_flags,
                                                   const char *asset_host,
                                                   const char *fs_root_dir)
{
    rl_init_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.window_width = window_width;
    cfg.window_height = window_height;
    cfg.window_title = (window_title != NULL && window_title[0] != '\0') ? window_title : NULL;
    cfg.window_flags = window_flags;
    cfg.asset_host = (asset_host != NULL && asset_host[0] != '\0') ? asset_host : NULL;
    cfg.fs_root_dir = (fs_root_dir != NULL && fs_root_dir[0] != '\0') ? fs_root_dir : NULL;
    return cfg;
}

RL_KEEP
int rl_init_values(int window_width,
                   int window_height,
                   const char *window_title,
                   unsigned int window_flags,
                   const char *asset_host,
                   const char *fs_root_dir) {
    rl_init_config_t cfg = build_init_config_from_values(
        window_width, window_height, window_title, window_flags, asset_host, fs_root_dir);

    return init_runtime_from_config(&cfg, false);
}

RL_KEEP
int rl_init_values_async(int window_width,
                         int window_height,
                         const char *window_title,
                         unsigned int window_flags,
                         const char *asset_host,
                         const char *fs_root_dir) {
    rl_init_config_t cfg = build_init_config_from_values(
        window_width, window_height, window_title, window_flags, asset_host, fs_root_dir);

    return init_runtime_from_config(&cfg, true);
}

RL_KEEP
bool rl_is_initialized(void) {
    return initialized;
}

RL_KEEP
const char *rl_get_platform(void) {
#if defined(PLATFORM_WEB)
    return "web";
#else
    return "desktop";
#endif
}

RL_KEEP
void rl_deinit() {
    if (!initialized) {
        return;
    }
    rl_scene_deinit();
    rl_camera3d_deinit();
    rl_debug_deinit();
    rl_sprite2d_deinit();
    rl_sprite3d_deinit();
    rl_text2d_deinit();
    rl_text3d_deinit();
    rl_shape_deinit();
    rl_texture_deinit();
    rl_model_deinit();
    rl_sound_deinit();
    rl_music_deinit();
    rl_event_deinit();
    rl_font_deinit();
    rl_color_deinit();
    if (IsAudioDeviceReady()) {
        CloseAudioDevice();
    }
    rl_scratch_deinit();
    rl_fs_deinit();
    initialized = false;
    rl_window_close_internal();
    rl_logger_deinit();
}

RL_KEEP
rl_tick_result_t rl_tick(void) {
    if (!initialized) {
        return RL_TICK_FAILED;
    }
    rl_asset_tick();
    if (!rl_fs_is_ready()) {
        return RL_TICK_WAITING;
    }
    return RL_TICK_RUNNING;
}

RL_KEEP
void rl_set_target_fps(int fps) {
    SetTargetFPS(fps);
}

RL_KEEP
float rl_get_delta_time() {
    return GetFrameTime();
}

RL_KEEP
double rl_get_time() {
    return GetTime();
}
