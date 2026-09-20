#ifndef WGRI_INTERNAL_ENVIRONMENT_H
#define WGRI_INTERNAL_ENVIRONMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "internal/wgr_math_internal.h"
#include "wgr_environment.h"
#include "wgr_types.h"
#include "sokol_gfx.h"

/* Environment lighting internals (docs/PLAN-environment.md): the CPU preparation
 * of an environment map, and what the model and background shaders bind.
 *
 * Conventions:
 * - Directions are world space, +y up.
 * - Equirectangular (latitude-longitude) images: u runs around the horizon with
 *   u = 0.5 looking toward -z and u increasing toward +x; v = 0 is straight up.
 * - Cubemap faces are ordered +X, -X, +Y, -Y, +Z, -Z with the OpenGL/sokol face
 *   orientation (s, t in 0..1, t = 0 at the first row of the face's data).
 * - Spherical harmonics are 9 RGB coefficients, already convolved for Lambert
 *   diffuse and divided by pi: evaluating them at a normal gives the irradiance
 *   divided by pi, so diffuse = albedo * wgri_environment_eval_sh(sh, n). */

#define WGRI_ENVIRONMENT_CUBE_SIZE 128   /* prefiltered cubemap face size (mip 0) */
#define WGRI_ENVIRONMENT_MIP_COUNT 6     /* 128 .. 4: roughness 0, 0.2, .. 1 */
#define WGRI_ENVIRONMENT_LUT_SIZE 64     /* BRDF lookup table (baked: src/data/wgr_brdf_lut.h) */
#define WGRI_ENVIRONMENT_LUT_SAMPLES 256

/* A linear RGB float image (3 floats per pixel). */
typedef struct {
    float *rgb;
    int width;
    int height;
} wgri_env_image_t;

/* A cubemap with a mip chain: mips[m] holds 6 faces of (size >> m)^2 RGB texels. */
typedef struct {
    int size;
    int mip_count;
    float *mips[16];
} wgri_env_cube_t;

typedef struct {
    float c[9][3];
} wgri_env_sh_t;

vec3_t wgri_environment_equirect_dir(float u, float v);
void   wgri_environment_dir_equirect(vec3_t dir, float *u, float *v);
vec3_t wgri_environment_cube_dir(int face, float s, float t);
void   wgri_environment_dir_cube(vec3_t dir, int *face, float *s, float *t);

vec3_t wgri_environment_sample_equirect(const wgri_env_image_t *image, vec3_t dir); /* bilinear */
vec3_t wgri_environment_sample_cube(const wgri_env_cube_t *cube, vec3_t dir, float lod); /* trilinear */

void   wgri_environment_project_sh(const wgri_env_image_t *image, wgri_env_sh_t *out);
vec3_t wgri_environment_eval_sh(const wgri_env_sh_t *sh, vec3_t normal);

/* Cubemap of `size` from an equirectangular image, with box-filtered mips down to 1. */
bool wgri_environment_cube_from_equirect(const wgri_env_image_t *image, int size, wgri_env_cube_t *out);
/* GGX-prefiltered cubemap: mip m is the source seen through roughness
 * m / (mip_count - 1), `samples` importance samples per texel. */
bool wgri_environment_prefilter(const wgri_env_cube_t *source, int size, int mip_count, int samples, wgri_env_cube_t *out);
void wgri_environment_cube_free(wgri_env_cube_t *cube);

/* Split-sum BRDF table: out[(y * size + x) * 2] = scale A, [+1] = bias B for
 * n_dot_v = (x + 0.5) / size and roughness = (y + 0.5) / size. */
void wgri_environment_brdf_lut(int size, int samples, float *out);

uint16_t wgri_environment_half_from_float(float value);

/* ------------------------------------------------------- runtime (GPU) ---- */

void wgri_environment_init(void);
void wgri_environment_deinit(void);
void wgri_environment_retain(wgr_handle_t environment);

/* What shaders bind for an environment (0 or invalid: a black cubemap and zero
 * lighting). Also returns the shared BRDF table and samplers. */
typedef struct {
    sg_view cube;
    sg_view brdf_lut;
    sg_sampler cube_sampler;
    sg_sampler lut_sampler;
    wgri_env_sh_t sh;
    float max_lod;
    bool valid;
} wgri_environment_binding_t;

void wgri_environment_get_binding(wgr_handle_t environment, wgri_environment_binding_t *out);

/* How other modules reach an environment without linking this one (sprites): set by
 * this module's init, cleared by its deinit, NULL while it isn't linked. Defined in
 * wgr_render.c. */
typedef struct {
    void (*get_binding)(wgr_handle_t environment, wgri_environment_binding_t *out);
} wgri_environment_hooks_t;
extern wgri_environment_hooks_t wgri_environment_hooks;

/* Queue the background (skybox) for the current render pass, drawn with the
 * active camera. Called by wgr_scene_draw before its 3D layers. */
void wgri_environment_submit_background(wgr_handle_t environment, float blur, float intensity, float rotation,
                                      int tonemap, float exposure);
void wgri_environment_end_frame(void);

#endif // WGRI_INTERNAL_ENVIRONMENT_H
