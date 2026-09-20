#ifndef WGRI_INTERNAL_SHADERS_H
#define WGRI_INTERNAL_SHADERS_H

/* The generated shaders (src/shaders/wgr_model.glsl.h, wgr_sprite.glsl.h, wgr_depth.glsl.h;
 * `make shaders`). sokol-shdc
 * wraps each backend's sources and descriptions in #if defined(SOKOL_<backend>), so a
 * build only carries its own backend's shaders.
 *
 * Headless builds use sokol's dummy backend, which compiles no source but still
 * validates a shader's layout (attributes, uniform blocks, textures). They take the
 * GL core descriptions, so SOKOL_GLCORE is defined just for this include (sokol_gfx.h
 * is already included and guarded; its implementation lives in wgr_sokol_impl.c). */

#include "sokol_gfx.h"

#if defined(SOKOL_DUMMY_BACKEND)
#  define SOKOL_GLCORE
#  include "shaders/wgr_model.glsl.h"
#  include "shaders/wgr_sprite.glsl.h"
#  include "shaders/wgr_depth.glsl.h"
#  undef SOKOL_GLCORE
#else
#  include "shaders/wgr_model.glsl.h"
#  include "shaders/wgr_sprite.glsl.h"
#  include "shaders/wgr_depth.glsl.h"
#endif

#endif // WGRI_INTERNAL_SHADERS_H
