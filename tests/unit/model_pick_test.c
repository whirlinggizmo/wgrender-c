#include "internal/wgr_model_internal.h"
#include "test.h"
#include "tests.h"

#define EPS 1e-4f

void test_model_skin_position(void)
{
    wgri_mat4_t joints[3] = {
        wgri_mat4_identity(),
        wgri_mat4_translate(0, 2, 0),
        wgri_mat4_trs((vec3_t){0, 0, 0}, (vec3_t){0, 0, 1.5707963f}, (vec3_t){1, 1, 1}), /* +90 degrees about z */
    };
    const vec3_t p = {1, 0, 0};

    /* single joint, full weight */
    vec3_t out = wgri_model_skin_position(joints, 3, p, (const uint8_t[4]){1, 0, 0, 0}, (const float[4]){1, 0, 0, 0});
    CHECK_VEC3_NEAR(out, 1, 2, 0, EPS);

    /* blend of two joints: halfway between translated (1,2,0) and rotated (0,1,0) */
    out = wgri_model_skin_position(joints, 3, p, (const uint8_t[4]){1, 2, 0, 0}, (const float[4]){0.5f, 0.5f, 0, 0});
    CHECK_VEC3_NEAR(out, 0.5f, 1.5f, 0, EPS);

    /* identity joints leave the bind pose unchanged */
    out = wgri_model_skin_position(joints, 3, (vec3_t){3, -4, 5}, (const uint8_t[4]){0, 0, 0, 0}, (const float[4]){1, 0, 0, 0});
    CHECK_VEC3_NEAR(out, 3, -4, 5, EPS);

    /* joint indices past the skin are ignored rather than read out of bounds */
    out = wgri_model_skin_position(joints, 3, p, (const uint8_t[4]){1, 200, 0, 0}, (const float[4]){0.5f, 0.5f, 0, 0});
    CHECK_VEC3_NEAR(out, 0.5f, 1.0f, 0, EPS);
}

void test_model_sample_alpha(void)
{
    /* 2x2: top row opaque, bottom row transparent */
    const uint8_t alpha[4] = {255, 255, 0, 0};

    const wgr_texture_wrap_t R = WGR_TEXTURE_WRAP_REPEAT, C = WGR_TEXTURE_WRAP_CLAMP, M = WGR_TEXTURE_WRAP_MIRROR;

    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 0.25f, R, R), 1.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.75f, 0.75f, R, R), 0.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 1.0f, 0.99f, R, R), 0.0f, EPS);   /* u = 1 wraps to 0 */
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 1.25f, R, R), 1.0f, EPS);  /* repeats past 1 */
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, -0.25f, -0.25f, R, R), 0.0f, EPS); /* repeats below 0: (0.75, 0.75) */
    CHECK_NEAR(wgri_model_sample_alpha(NULL, 0, 0, 0.5f, 0.5f, R, R), 1.0f, EPS);     /* no texture: opaque */

    /* clamp: past the edge stays on the edge row */
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 1.25f, R, C), 0.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, -3.0f, R, C), 1.0f, EPS);
    /* mirror: 1.25 reflects to 0.75, 1.75 to 0.25, 2.25 repeats to 0.25 */
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 1.25f, R, M), 0.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 1.75f, R, M), 1.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, 2.25f, R, M), 1.0f, EPS);
    CHECK_NEAR(wgri_model_sample_alpha(alpha, 2, 2, 0.25f, -0.25f, R, M), 1.0f, EPS); /* reflects to 0.25 */
}
