#ifndef WGR_INTERNAL_ENVIRONMENT_H
#define WGR_INTERNAL_ENVIRONMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "internal/wgr_math.h"
#include <wgr_environment.h> /* the public header ("" would find this file); wgr_environment_release lives there */
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
 *   divided by pi, so diffuse = albedo * wgr_environment_eval_sh(sh, n). */

#define WGR_ENVIRONMENT_CUBE_SIZE 128   /* prefiltered cubemap face size (mip 0) */
#define WGR_ENVIRONMENT_MIP_COUNT 6     /* 128 .. 4: roughness 0, 0.2, .. 1 */
#define WGR_ENVIRONMENT_LUT_SIZE 64     /* BRDF lookup table (baked: src/data/wgr_brdf_lut.h) */
#define WGR_ENVIRONMENT_LUT_SAMPLES 256

/* A linear RGB float image (3 floats per pixel). */
typedef struct {
    float *rgb;
    int width;
    int height;
} wgr_env_image_t;

/* A cubemap with a mip chain: mips[m] holds 6 faces of (size >> m)^2 RGB texels. */
typedef struct {
    int size;
    int mip_count;
    float *mips[16];
} wgr_env_cube_t;

typedef struct {
    float c[9][3];
} wgr_env_sh_t;

vec3_t wgr_environment_equirect_dir(float u, float v);
void   wgr_environment_dir_equirect(vec3_t dir, float *u, float *v);
vec3_t wgr_environment_cube_dir(int face, float s, float t);
void   wgr_environment_dir_cube(vec3_t dir, int *face, float *s, float *t);

vec3_t wgr_environment_sample_equirect(const wgr_env_image_t *image, vec3_t dir); /* bilinear */
vec3_t wgr_environment_sample_cube(const wgr_env_cube_t *cube, vec3_t dir, float lod); /* trilinear */

void   wgr_environment_project_sh(const wgr_env_image_t *image, wgr_env_sh_t *out);
vec3_t wgr_environment_eval_sh(const wgr_env_sh_t *sh, vec3_t normal);

/* Cubemap of `size` from an equirectangular image, with box-filtered mips down to 1. */
bool wgr_environment_cube_from_equirect(const wgr_env_image_t *image, int size, wgr_env_cube_t *out);
/* GGX-prefiltered cubemap: mip m is the source seen through roughness
 * m / (mip_count - 1), `samples` importance samples per texel. */
bool wgr_environment_prefilter(const wgr_env_cube_t *source, int size, int mip_count, int samples, wgr_env_cube_t *out);
void wgr_environment_cube_free(wgr_env_cube_t *cube);

/* Split-sum BRDF table: out[(y * size + x) * 2] = scale A, [+1] = bias B for
 * n_dot_v = (x + 0.5) / size and roughness = (y + 0.5) / size. */
void wgr_environment_brdf_lut(int size, int samples, float *out);

uint16_t wgr_environment_half_from_float(float value);

/* ------------------------------------------------------- runtime (GPU) ---- */

void wgr_environment_init(void);
void wgr_environment_deinit(void);
void wgr_environment_retain(wgr_handle_t environment);

/* What shaders bind for an environment (0 or invalid: a black cubemap and zero
 * lighting). Also returns the shared BRDF table and samplers. */
typedef struct {
    sg_view cube;
    sg_view brdf_lut;
    sg_sampler cube_sampler;
    sg_sampler lut_sampler;
    wgr_env_sh_t sh;
    float max_lod;
    bool valid;
} wgr_environment_binding_t;

void wgr_environment_get_binding(wgr_handle_t environment, wgr_environment_binding_t *out);

/* How other modules reach an environment without linking this one (sprites): set by
 * this module's init, cleared by its deinit, NULL while it isn't linked. Defined in
 * wgr_render.c. */
typedef struct {
    void (*get_binding)(wgr_handle_t environment, wgr_environment_binding_t *out);
} wgr_environment_hooks_t;
extern wgr_environment_hooks_t wgr_environment_hooks;

/* Queue the background (skybox) for the current render pass, drawn with the
 * active camera. Called by wgr_scene_draw before its 3D layers. */
void wgr_environment_submit_background(wgr_handle_t environment, float blur, float intensity, float rotation,
                                      int tonemap, float exposure);
void wgr_environment_end_frame(void);

#endif // WGR_INTERNAL_ENVIRONMENT_H
