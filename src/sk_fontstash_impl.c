/* Implementation TU for fontstash + the sokol_fontstash renderer.
 *
 * sokol_gfx/sokol_gl/etc. are implemented in sk_sokol_impl.c; here we only need
 * their declarations. fontstash.h pulls in stb_truetype's implementation. */
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_GLES3) && !defined(SOKOL_D3D11) && \
    !defined(SOKOL_METAL) && !defined(SOKOL_WGPU)
#define SOKOL_GLCORE
#endif

/* fontstash.h's implementation uses libc facilities (FILE, size_t, malloc,
 * memset, math) without including the headers itself. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

#define SOKOL_FONTSTASH_IMPL
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"
