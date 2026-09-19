#ifndef SK_HANDLE_H
#define SK_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/** Resource kind encoded in the high 6 bits of {@link sk_handle_t}. */
typedef enum sk_handle_kind_t {
    SK_HANDLE_KIND_NONE = 0,
    /* 1 retired: colors are values (sk_color_t), not handles */
    SK_HANDLE_KIND_CAMERA3D = 2,
    SK_HANDLE_KIND_FONT = 3,
    SK_HANDLE_KIND_TEXTURE = 4,
    SK_HANDLE_KIND_SPRITE2D = 5,
    SK_HANDLE_KIND_SPRITE3D = 6,
    SK_HANDLE_KIND_MODEL = 7,
    SK_HANDLE_KIND_MESH = 8,
    SK_HANDLE_KIND_SOUND = 9,
    /* 10 retired: music folded into Sound (a looping sound over an Audio) */
    SK_HANDLE_KIND_TEXT2D = 11,
    SK_HANDLE_KIND_SCENE = 12,
    SK_HANDLE_KIND_SHAPE3D = 13,
    SK_HANDLE_KIND_TEXT3D = 14,
    SK_HANDLE_KIND_AUDIO = 15, /* resource: decoded PCM shared by Sound/Music */
    SK_HANDLE_KIND_LIGHT = 16, /* object: directional / point / spot light */
    SK_HANDLE_KIND_MATERIAL = 17, /* resource: shading model + parameters + textures */
    SK_HANDLE_KIND_ENVIRONMENT = 18, /* resource: prefiltered environment map for lighting */
    SK_HANDLE_KIND_SHAPE2D = 19,  /* object: screen-space shape (rectangle, circle, line) */
    SK_HANDLE_KIND_EMITTER3D = 20, /* object: particles in the world */
    SK_HANDLE_KIND_EMITTER2D = 21, /* object: particles in screen space */
    /* 22-31 reserved for future drawable / presentation / resource kinds */
    SK_HANDLE_KIND_ASSET_TASK = 32,
} sk_handle_kind_t;

/** Returns {@link SK_HANDLE_KIND_NONE} for handle {@code 0}; otherwise the kind field. */
sk_handle_kind_t sk_handle_get_kind(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_HANDLE_H
