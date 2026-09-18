#ifndef SK_INTERNAL_SHADERS_H
#define SK_INTERNAL_SHADERS_H

/* The generated shaders (src/shaders/sk_model.glsl.h, sk_sprite.glsl.h; `make shaders`). sokol-shdc
 * wraps each backend's sources and descriptions in #if defined(SOKOL_<backend>), so a
 * build only carries its own backend's shaders.
 *
 * Headless builds use sokol's dummy backend, which compiles no source but still
 * validates a shader's layout (attributes, uniform blocks, textures). They take the
 * GL core descriptions, so SOKOL_GLCORE is defined just for this include (sokol_gfx.h
 * is already included and guarded; its implementation lives in sk_sokol_impl.c). */

#include "sokol_gfx.h"

#if defined(SOKOL_DUMMY_BACKEND)
#  define SOKOL_GLCORE
#  include "shaders/sk_model.glsl.h"
#  include "shaders/sk_sprite.glsl.h"
#  undef SOKOL_GLCORE
#else
#  include "shaders/sk_model.glsl.h"
#  include "shaders/sk_sprite.glsl.h"
#endif

#endif // SK_INTERNAL_SHADERS_H
