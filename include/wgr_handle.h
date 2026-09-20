#ifndef WGR_HANDLE_H
#define WGR_HANDLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/** Resource kind encoded in the high 6 bits of {@link wgr_handle_t}. */
typedef enum wgr_handle_kind_t {
    WGR_HANDLE_KIND_NONE = 0,
    /* 1 retired: colors are values (wgr_color_t), not handles */
    WGR_HANDLE_KIND_CAMERA3D = 2,
    WGR_HANDLE_KIND_FONT = 3,
    WGR_HANDLE_KIND_TEXTURE = 4,
    WGR_HANDLE_KIND_SPRITE2D = 5,
    WGR_HANDLE_KIND_SPRITE3D = 6,
    WGR_HANDLE_KIND_MODEL = 7,
    WGR_HANDLE_KIND_MESH = 8,
    WGR_HANDLE_KIND_SOUND = 9,
    /* 10 retired: music folded into Sound (a looping sound over an Audio) */
    WGR_HANDLE_KIND_TEXT2D = 11,
    WGR_HANDLE_KIND_SCENE = 12,
    WGR_HANDLE_KIND_SHAPE3D = 13,
    WGR_HANDLE_KIND_TEXT3D = 14,
    WGR_HANDLE_KIND_AUDIO = 15, /* resource: decoded PCM shared by Sound/Music */
    WGR_HANDLE_KIND_LIGHT = 16, /* object: directional / point / spot light */
    WGR_HANDLE_KIND_MATERIAL = 17, /* resource: shading model + parameters + textures */
    WGR_HANDLE_KIND_ENVIRONMENT = 18, /* resource: prefiltered environment map for lighting */
    WGR_HANDLE_KIND_SHAPE2D = 19,  /* object: screen-space shape (rectangle, circle, line) */
    WGR_HANDLE_KIND_EMITTER3D = 20, /* object: particles in the world */
    WGR_HANDLE_KIND_EMITTER2D = 21, /* object: particles in screen space */
    WGR_HANDLE_KIND_SHADER = 22,    /* resource: a custom material shader (.wgrshader) */
    /* 23-31 reserved for future drawable / presentation / resource kinds */
    WGR_HANDLE_KIND_ASSET_TASK = 32,
} wgr_handle_kind_t;

/** Returns {@link WGR_HANDLE_KIND_NONE} for handle {@code 0}; otherwise the kind field. */
wgr_handle_kind_t wgr_handle_get_kind(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_HANDLE_H
