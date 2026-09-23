/* Every kind with a transform has the same calls for it: a setter for the whole thing,
 * one per part that leaves the others alone, and a getter per part. Checked here for
 * each kind, on sokol's dummy backend: the setters agree with each other, a part set
 * alone leaves the rest as it was, and a handle that isn't one is refused and reads 0. */
#include <math.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_model_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite2d_internal.h"
#include "wgr_logger.h"
#include "wgr_model.h"
#include "wgr_shape2d.h"
#include "wgr_shape3d.h"
#include "wgr_sprite2d.h"
#include "wgr_sprite3d.h"
#include "wgr_text2d.h"
#include "wgr_text3d.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-5f

#define CHECK_VEC2_NEAR(v, ex, ey)                                                     \
    do {                                                                               \
        CHECK_NEAR((v).x, (ex), EPS);                                                  \
        CHECK_NEAR((v).y, (ey), EPS);                                                  \
    } while (0)

/* The same checks for every 3D kind with a scale: they differ only in their names.
 * `h` is used many times, so pass a variable, not a create call. */
#define CHECK_TRANSFORM3D(kind, h)                                                     \
    do {                                                                               \
        CHECK(wgr_##kind##_set_transform(h, 1, 2, 3, 0.1f, 0.2f, 0.3f, 4, 5, 6));      \
        CHECK_VEC3_NEAR(wgr_##kind##_get_position(h), 1, 2, 3, EPS);                   \
        CHECK_VEC3_NEAR(wgr_##kind##_get_rotation(h), 0.1f, 0.2f, 0.3f, EPS);          \
        CHECK_VEC3_NEAR(wgr_##kind##_get_scale(h), 4, 5, 6, EPS);                      \
        CHECK(wgr_##kind##_set_position(h, 7, 8, 9));                                  \
        CHECK_VEC3_NEAR(wgr_##kind##_get_position(h), 7, 8, 9, EPS);                   \
        CHECK_VEC3_NEAR(wgr_##kind##_get_rotation(h), 0.1f, 0.2f, 0.3f, EPS);          \
        CHECK_VEC3_NEAR(wgr_##kind##_get_scale(h), 4, 5, 6, EPS);                      \
        CHECK(wgr_##kind##_set_rotation(h, 0.4f, 0.5f, 0.6f));                         \
        CHECK_VEC3_NEAR(wgr_##kind##_get_rotation(h), 0.4f, 0.5f, 0.6f, EPS);          \
        CHECK_VEC3_NEAR(wgr_##kind##_get_position(h), 7, 8, 9, EPS);                   \
        CHECK(wgr_##kind##_set_scale(h, 2, 2, 2));                                     \
        CHECK_VEC3_NEAR(wgr_##kind##_get_scale(h), 2, 2, 2, EPS);                      \
        CHECK_VEC3_NEAR(wgr_##kind##_get_rotation(h), 0.4f, 0.5f, 0.6f, EPS);          \
        CHECK(!wgr_##kind##_set_position(0, 1, 1, 1));                                 \
        CHECK(!wgr_##kind##_set_rotation(0, 1, 1, 1));                                 \
        CHECK(!wgr_##kind##_set_scale(0, 1, 1, 1));                                    \
        CHECK_VEC3_NEAR(wgr_##kind##_get_position(0), 0, 0, 0, EPS);                   \
        CHECK_VEC3_NEAR(wgr_##kind##_get_rotation(0), 0, 0, 0, EPS);                   \
        CHECK_VEC3_NEAR(wgr_##kind##_get_scale(0), 0, 0, 0, EPS);                      \
    } while (0)

/* And for every 2D kind: a position, an angle and a scale. */
#define CHECK_TRANSFORM2D(kind, h)                                                     \
    do {                                                                               \
        CHECK(wgr_##kind##_set_transform(h, 10, 20, 0.5f, 2, 3));                      \
        CHECK_VEC2_NEAR(wgr_##kind##_get_position(h), 10, 20);                         \
        CHECK_NEAR(wgr_##kind##_get_rotation(h), 0.5f, EPS);                           \
        CHECK_VEC2_NEAR(wgr_##kind##_get_scale(h), 2, 3);                              \
        CHECK(wgr_##kind##_set_position(h, 30, 40));                                   \
        CHECK_VEC2_NEAR(wgr_##kind##_get_position(h), 30, 40);                         \
        CHECK_NEAR(wgr_##kind##_get_rotation(h), 0.5f, EPS);                           \
        CHECK(wgr_##kind##_set_rotation(h, 1.25f));                                    \
        CHECK_NEAR(wgr_##kind##_get_rotation(h), 1.25f, EPS);                          \
        CHECK_VEC2_NEAR(wgr_##kind##_get_scale(h), 2, 3);                              \
        CHECK(wgr_##kind##_set_scale(h, -1, 1));                                       \
        CHECK_VEC2_NEAR(wgr_##kind##_get_scale(h), -1, 1);                             \
        CHECK_VEC2_NEAR(wgr_##kind##_get_position(h), 30, 40);                         \
        CHECK(!wgr_##kind##_set_position(0, 1, 1));                                    \
        CHECK(!wgr_##kind##_set_rotation(0, 1));                                       \
        CHECK(!wgr_##kind##_set_scale(0, 1, 1));                                       \
        CHECK_VEC2_NEAR(wgr_##kind##_get_position(0), 0, 0);                           \
        CHECK_NEAR(wgr_##kind##_get_rotation(0), 0, EPS);                              \
        CHECK_VEC2_NEAR(wgr_##kind##_get_scale(0), 0, 0);                              \
    } while (0)

void test_transforms(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_sprite3d_init();
    wgri_sprite2d_init();
    wgri_material_init();
    wgri_model_init();
    wgri_shape3d_init();
    wgri_shape2d_init();
    wgri_font_init();
    wgri_text3d_init();
    wgri_text2d_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL);

    const wgr_handle_t sprite3d = wgr_sprite3d_create(0);
    const wgr_handle_t model = wgr_model_create(0);
    const wgr_handle_t shape3d = wgr_shape3d_create();
    const wgr_handle_t sprite2d = wgr_sprite2d_create(0);
    const wgr_handle_t shape2d = wgr_shape2d_create();
    CHECK_TRANSFORM3D(sprite3d, sprite3d);
    CHECK_TRANSFORM3D(model, model);
    CHECK_TRANSFORM3D(shape3d, shape3d);
    CHECK_TRANSFORM2D(sprite2d, sprite2d);
    CHECK_TRANSFORM2D(shape2d, shape2d);

    /* 3D text: a position and a rotation; its size stands in for a scale */
    wgr_handle_t text3d = wgr_text3d_create(0);
    CHECK(wgr_text3d_set_transform(text3d, 1, 2, 3, 0.1f, 0.2f, 0.3f));
    CHECK_VEC3_NEAR(wgr_text3d_get_position(text3d), 1, 2, 3, EPS);
    CHECK_VEC3_NEAR(wgr_text3d_get_rotation(text3d), 0.1f, 0.2f, 0.3f, EPS);
    CHECK(wgr_text3d_set_position(text3d, 4, 5, 6));
    CHECK_VEC3_NEAR(wgr_text3d_get_rotation(text3d), 0.1f, 0.2f, 0.3f, EPS);
    CHECK(wgr_text3d_set_rotation(text3d, 0.7f, 0.8f, 0.9f));
    CHECK_VEC3_NEAR(wgr_text3d_get_position(text3d), 4, 5, 6, EPS);
    CHECK(!wgr_text3d_set_position(0, 1, 1, 1) && !wgr_text3d_set_rotation(0, 1, 1, 1));
    CHECK_VEC3_NEAR(wgr_text3d_get_position(0), 0, 0, 0, EPS);

    /* 2D text: a position only */
    wgr_handle_t text2d = wgr_text2d_create(0);
    CHECK(wgr_text2d_set_position(text2d, 12, 34));
    CHECK_VEC2_NEAR(wgr_text2d_get_position(text2d), 12, 34);
    CHECK_VEC2_NEAR(wgr_text2d_get_position(0), 0, 0);

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_text2d_deinit();
    wgri_text3d_deinit();
    wgri_font_deinit();
    wgri_shape2d_deinit();
    wgri_shape3d_deinit();
    wgri_model_deinit();
    wgri_material_deinit();
    wgri_sprite2d_deinit();
    wgri_sprite3d_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    sg_shutdown();
}
