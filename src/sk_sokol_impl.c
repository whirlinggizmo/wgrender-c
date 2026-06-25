/* Single translation unit that compiles the sokol implementations.
 * Backend is selected by the build (SOKOL_GLCORE for desktop GL). */
#if !defined(SOKOL_GLCORE) && !defined(SOKOL_GLES3) && !defined(SOKOL_D3D11) && \
    !defined(SOKOL_METAL) && !defined(SOKOL_WGPU)
#define SOKOL_GLCORE
#endif

#define SOKOL_IMPL

/* libsk is a library: the consumer owns main() and calls sk_run(), which calls
 * sapp_run() internally. SOKOL_NO_ENTRY stops sokol_app from generating its own
 * main()/sokol_main() entry point. */
#define SOKOL_NO_ENTRY

#include "sokol_log.h"
#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_time.h"
#include "sokol_fetch.h"
#include "sokol_audio.h"
#include "util/sokol_gl.h"
#include "util/sokol_debugtext.h"
