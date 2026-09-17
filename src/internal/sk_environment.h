#ifndef SK_INTERNAL_ENVIRONMENT_H
#define SK_INTERNAL_ENVIRONMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "internal/sk_math.h"
#include "sk_types.h"
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
 *   divided by pi, so diffuse = albedo * sk_environment_eval_sh(sh, n). */

#define SK_ENVIRONMENT_CUBE_SIZE 128   /* prefiltered cubemap face size (mip 0) */
#define SK_ENVIRONMENT_MIP_COUNT 6     /* 128 .. 4: roughness 0, 0.2, .. 1 */
#define SK_ENVIRONMENT_LUT_SIZE 64     /* BRDF lookup table */

/* A linear RGB float image (3 floats per pixel). */
typedef struct {
    float *rgb;
    int width;
    int height;
} sk_env_image_t;

/* A cubemap with a mip chain: mips[m] holds 6 faces of (size >> m)^2 RGB texels. */
typedef struct {
    int size;
    int mip_count;
    float *mips[16];
} sk_env_cube_t;

typedef struct {
    float c[9][3];
} sk_env_sh_t;

vec3_t sk_environment_equirect_dir(float u, float v);
void   sk_environment_dir_equirect(vec3_t dir, float *u, float *v);
vec3_t sk_environment_cube_dir(int face, float s, float t);
void   sk_environment_dir_cube(vec3_t dir, int *face, float *s, float *t);

vec3_t sk_environment_sample_equirect(const sk_env_image_t *image, vec3_t dir); /* bilinear */
vec3_t sk_environment_sample_cube(const sk_env_cube_t *cube, vec3_t dir, float lod); /* trilinear */

void   sk_environment_project_sh(const sk_env_image_t *image, sk_env_sh_t *out);
vec3_t sk_environment_eval_sh(const sk_env_sh_t *sh, vec3_t normal);

/* Cubemap of `size` from an equirectangular image, with box-filtered mips down to 1. */
bool sk_environment_cube_from_equirect(const sk_env_image_t *image, int size, sk_env_cube_t *out);
/* GGX-prefiltered cubemap: mip m is the source seen through roughness
 * m / (mip_count - 1), `samples` importance samples per texel. */
bool sk_environment_prefilter(const sk_env_cube_t *source, int size, int mip_count, int samples, sk_env_cube_t *out);
void sk_environment_cube_free(sk_env_cube_t *cube);

/* Split-sum BRDF table: out[(y * size + x) * 2] = scale A, [+1] = bias B for
 * n_dot_v = (x + 0.5) / size and roughness = (y + 0.5) / size. */
void sk_environment_brdf_lut(int size, int samples, float *out);

uint16_t sk_environment_half_from_float(float value);

/* ------------------------------------------------------- runtime (GPU) ---- */

void sk_environment_init(void);
void sk_environment_deinit(void);
void sk_environment_retain(sk_handle_t environment);
void sk_environment_release(sk_handle_t environment);

/* What shaders bind for an environment (0 or invalid: a black cubemap and zero
 * lighting). Also returns the shared BRDF table and samplers. */
typedef struct {
    sg_view cube;
    sg_view brdf_lut;
    sg_sampler cube_sampler;
    sg_sampler lut_sampler;
    sk_env_sh_t sh;
    float max_lod;
    bool valid;
} sk_environment_binding_t;

void sk_environment_get_binding(sk_handle_t environment, sk_environment_binding_t *out);

/* Queue the background (skybox) for the current render pass, drawn with the
 * active camera. Called by sk_scene_draw before its 3D layers. */
void sk_environment_submit_background(sk_handle_t environment, float blur, float intensity, float rotation,
                                      int tonemap, float exposure);
void sk_environment_end_frame(void);

#endif // SK_INTERNAL_ENVIRONMENT_H
