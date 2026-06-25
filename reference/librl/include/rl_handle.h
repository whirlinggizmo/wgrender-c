#ifndef RL_HANDLE_H
#define RL_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "rl_types.h"

/** Resource kind encoded in the high 6 bits of {@link rl_handle_t}. */
typedef enum rl_handle_kind_t {
    RL_HANDLE_KIND_NONE = 0,
    RL_HANDLE_KIND_COLOR = 1,
    RL_HANDLE_KIND_CAMERA3D = 2,
    RL_HANDLE_KIND_FONT = 3,
    RL_HANDLE_KIND_TEXTURE = 4,
    RL_HANDLE_KIND_SPRITE2D = 5,
    RL_HANDLE_KIND_SPRITE3D = 6,
    RL_HANDLE_KIND_MODEL = 7,
    RL_HANDLE_KIND_MODEL_ASSET = 8,
    RL_HANDLE_KIND_SOUND = 9,
    RL_HANDLE_KIND_MUSIC = 10,
    RL_HANDLE_KIND_TEXT2D = 11,
    RL_HANDLE_KIND_SCENE = 12,
    RL_HANDLE_KIND_SHAPE = 13,
    RL_HANDLE_KIND_TEXT3D = 14,
    /* 15-31 reserved for future drawable / presentation kinds */
    RL_HANDLE_KIND_ASSET_TASK = 32,
} rl_handle_kind_t;

/** Returns {@link RL_HANDLE_KIND_NONE} for handle {@code 0}; otherwise the kind field. */
rl_handle_kind_t rl_handle_get_kind(rl_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // RL_HANDLE_H
