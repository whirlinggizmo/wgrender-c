/* Skeletal animation sampling (src/wgr_model.c): posing a clip at a time samples its
 * keyframes and composes the node hierarchy into joint matrices. The pose itself is
 * what's checked here — wgr_model_get_joint_matrices — since it's what the skinned
 * shader and CPU picking both read. Timing (wrap, clamp, a time set before the mesh
 * arrives) is covered by test_parity_model. */
#include <math.h>
#include <string.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_model.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define GUMSHOE "../examples/assets/models/gumshoe/gumshoe.glb"
#define MAX_FLOATS (128 * 16)

/* The model's pose, copied so it survives the next posing. Returns the float count. */
static int snapshot(wgr_handle_t model, float *out)
{
    const float *matrices = NULL;
    const int joints = wgr_model_get_joint_matrices(model, &matrices);
    const int floats = joints * 16 < MAX_FLOATS ? joints * 16 : MAX_FLOATS;
    if (matrices != NULL) memcpy(out, matrices, (size_t)floats * sizeof(float));
    return floats;
}

/* How far apart two poses are: the largest difference of any matrix element. */
static float pose_distance(const float *a, const float *b, int floats)
{
    float worst = 0.0f;
    for (int i = 0; i < floats; i++) {
        const float d = fabsf(a[i] - b[i]);
        if (d > worst) worst = d;
    }
    return worst;
}

void test_animation_sampling(void)
{
    static float rest[MAX_FLOATS], start[MAX_FLOATS], middle[MAX_FLOATS], other[MAX_FLOATS];

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_light_init();
    wgr_material_init();
    wgr_environment_init();
    wgr_model_init();

    const wgr_handle_t mesh = wgr_mesh_create(GUMSHOE);
    const wgr_handle_t model = wgr_model_create(mesh);
    CHECK(mesh != 0 && model != 0);
    wgr_mesh_release(mesh); /* the model holds it */

    const int count = wgr_model_get_animation_count(model);
    CHECK(count > 0);
    const float duration = wgr_model_get_animation_duration(model, 0);
    CHECK(duration > 0.0f);
    CHECK(wgr_model_get_animation_duration(model, count) == 0.0f); /* no such clip */
    CHECK(wgr_model_get_animation_duration(model, -1) == 0.0f);

    /* an unposed skinned model is in its bind pose: joints, but no clip applied */
    const int floats = snapshot(model, rest);
    CHECK(floats > 0);

    /* posing samples the clip: the start of a clip is not the bind pose, and the
       middle is not the start */
    CHECK(wgr_model_set_animation(model, 0));
    CHECK(wgr_model_set_animation_time(model, 0.0f));
    CHECK(snapshot(model, start) == floats);
    CHECK(pose_distance(rest, start, floats) > 1e-4f);
    CHECK(wgr_model_set_animation_time(model, duration * 0.5f));
    CHECK(snapshot(model, middle) == floats);
    CHECK(pose_distance(start, middle, floats) > 1e-4f);

    /* the same time is the same pose, every time */
    CHECK(wgr_model_set_animation_time(model, 0.0f));
    CHECK(snapshot(model, other) == floats);
    CHECK(pose_distance(start, other, floats) == 0.0f);

    /* sampling is continuous: a small step in time moves the pose much less than half
       the clip does (keyframes are interpolated, not jumped between) */
    CHECK(wgr_model_set_animation_time(model, duration * 0.01f));
    CHECK(snapshot(model, other) == floats);
    const float nearby = pose_distance(start, other, floats);
    CHECK(nearby > 0.0f && nearby < pose_distance(start, middle, floats));

    /* looping: a time past the end is the pose of its wrapped time (the wrapped time
       lands a rounding step from the middle, so the pose does too) */
    CHECK(wgr_model_set_animation_loop(model, true));
    CHECK(wgr_model_set_animation_time(model, duration + duration * 0.5f));
    CHECK_NEAR(wgr_model_get_animation_time(model), duration * 0.5f, 1e-4f);
    CHECK(snapshot(model, other) == floats);
    CHECK(pose_distance(middle, other, floats) < 1e-4f);

    /* and a negative time wraps back into the clip */
    CHECK(wgr_model_set_animation_time(model, -duration * 0.5f));
    CHECK_NEAR(wgr_model_get_animation_time(model), duration * 0.5f, 1e-4f);
    CHECK(snapshot(model, other) == floats);
    CHECK(pose_distance(middle, other, floats) < 1e-4f);

    /* not looping: past the end is the end, and before the start is the start */
    CHECK(wgr_model_set_animation_loop(model, false));
    CHECK(wgr_model_set_animation_time(model, duration * 10.0f));
    CHECK_NEAR(wgr_model_get_animation_time(model), duration, 1e-4f);
    CHECK(wgr_model_set_animation_time(model, -5.0f));
    CHECK_NEAR(wgr_model_get_animation_time(model), 0.0f, 1e-4f);
    CHECK(snapshot(model, other) == floats);
    CHECK(pose_distance(start, other, floats) == 0.0f);

    /* speed scales what a frame advances, and runs the clip backwards when negative */
    CHECK(wgr_model_set_animation_loop(model, true));
    CHECK(wgr_model_set_animation_time(model, 0.0f));
    CHECK(wgr_model_set_animation_speed(model, 2.0f));
    CHECK(wgr_model_animate(model, 0.1f));
    CHECK_NEAR(wgr_model_get_animation_time(model), 0.2f, 1e-5f);
    CHECK(wgr_model_animate(model, 0.1f));
    CHECK_NEAR(wgr_model_get_animation_time(model), 0.4f, 1e-5f);
    CHECK(wgr_model_set_animation_speed(model, -1.0f));
    CHECK(wgr_model_animate(model, 0.1f));
    CHECK_NEAR(wgr_model_get_animation_time(model), 0.3f, 1e-5f);
    CHECK(wgr_model_set_animation_speed(model, 0.0f)); /* frozen: the pose stays put */
    CHECK(snapshot(model, other) == floats);
    CHECK(wgr_model_animate(model, 1.0f));
    CHECK_NEAR(wgr_model_get_animation_time(model), 0.3f, 1e-5f);
    CHECK(snapshot(model, start) == floats);
    CHECK(pose_distance(start, other, floats) == 0.0f);
    CHECK(wgr_model_set_animation_speed(model, 1.0f));

    /* a clip that isn't there leaves the pose alone */
    CHECK(snapshot(model, other) == floats);
    CHECK(wgr_model_set_animation(model, count + 5));
    CHECK(!wgr_model_animate(model, 0.1f));
    CHECK(snapshot(model, start) == floats);
    CHECK(pose_distance(start, other, floats) == 0.0f);

    /* a model with no mesh has no joints and nothing to animate */
    const wgr_handle_t empty = wgr_model_create(0);
    CHECK(wgr_model_get_joint_matrices(empty, NULL) == 0);
    CHECK(!wgr_model_animate(empty, 0.1f));
    CHECK(wgr_model_get_animation_count(empty) == 0);
    wgr_model_destroy(empty);

    wgr_model_destroy(model);
    wgr_model_deinit();
    wgr_environment_deinit();
    wgr_material_deinit();
    wgr_light_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}
