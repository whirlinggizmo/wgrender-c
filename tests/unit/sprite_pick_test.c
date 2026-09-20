/* Alpha-test picking on 3D sprites (wgr_sprite3d_set_pick_alpha_test): a sprite is a
 * quad, but its texture is usually not — with the alpha test on, a ray through a
 * transparent texel misses. The CPU alpha mask behind it (src/wgr_texture.c) is built
 * from the texture's source file on demand, so a texture made from pixels in memory
 * has none and can't reject anything. */
#include <math.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_camera3d.h"
#include "wgr_logger.h"
#include "wgr_pick.h"
#include "wgr_sprite3d.h"
#include "wgr_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define BLOB "../examples/assets/textures/blobshadow.png"
#define SCREEN 101.0f /* logical pixels each way, so the middle is a whole pixel */

/* The furthest the sprite can be picked along one screen axis from the middle: the
 * quad's edge, found by walking out until picks stop hitting. */
static float edge_from_middle(wgr_handle_t sprite, wgr_handle_t camera, bool along_x)
{
    const float middle = SCREEN * 0.5f;
    float last = 0.0f;
    for (float d = 1.0f; d < middle; d += 1.0f) {
        const float x = along_x ? middle + d : middle;
        const float y = along_x ? middle : middle + d;
        if (!wgr_pick_object(sprite, camera, x, y).hit) break;
        last = d;
    }
    return last;
}

void test_sprite_pick_alpha(void)
{
    const float middle = SCREEN * 0.5f;
    const unsigned char *mask = NULL;
    int width = 0, height = 0;
    float centre_alpha = 0.0f, corner_alpha = 0.0f;

    /* other tests pick on the headless default screen: put it back at the end */
    const int was_width = wgri_platform_width(), was_height = wgri_platform_height();

    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_platform_set_window_size((int)SCREEN, (int)SCREEN);
    wgri_render_init();
    wgri_scene_init();
    wgri_camera3d_init();
    wgri_texture_init();
    wgri_material_init();
    wgri_sprite3d_init();

    /* a round blob on a square texture: opaque in the middle, clear at the corners */
    const wgr_handle_t texture = wgr_texture_create(BLOB);
    CHECK(texture != 0);
    CHECK(wgri_texture_ensure_alpha_mask(texture));
    CHECK(wgri_texture_get_alpha_mask(texture, &mask, &width, &height));
    CHECK(mask != NULL && width > 0 && height > 0);
    CHECK(wgri_texture_sample_alpha(texture, 0.5f, 0.5f, &centre_alpha));
    CHECK(wgri_texture_sample_alpha(texture, 0.02f, 0.02f, &corner_alpha));
    CHECK(centre_alpha > 0.9f && corner_alpha < 0.1f);
    /* outside 0..1 the mask wraps like the sampler, so a corner stays a corner */
    float wrapped = -1.0f;
    CHECK(wgri_texture_sample_alpha(texture, 1.02f, 1.02f, &wrapped));
    CHECK_NEAR(wrapped, corner_alpha, 1e-6f);

    const wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 3, 0, 0, 0, 0, 1, 0);
    const wgr_handle_t sprite = wgr_sprite3d_create(texture);
    wgr_sprite3d_set_transform(sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);

    /* the whole quad picks while the alpha test is off */
    CHECK(wgr_pick_object(sprite, camera, middle, middle).hit);
    const float right = edge_from_middle(sprite, camera, true);
    const float down = edge_from_middle(sprite, camera, false);
    CHECK(right > 4.0f && down > 4.0f); /* it covers a good part of the screen */
    const float corner_x = middle + right * 0.85f, corner_y = middle + down * 0.85f;
    CHECK(wgr_pick_object(sprite, camera, corner_x, corner_y).hit);

    /* with it on, the clear corner of the quad is no longer hit, but the blob is */
    CHECK(wgr_sprite3d_set_pick_alpha_test(sprite, true, 0.5f));
    CHECK(wgr_pick_object(sprite, camera, middle, middle).hit);
    CHECK(!wgr_pick_object(sprite, camera, corner_x, corner_y).hit);

    /* the threshold decides: nothing is below 0, so everything is hit again */
    CHECK(wgr_sprite3d_set_pick_alpha_test(sprite, true, 0.0f));
    CHECK(wgr_pick_object(sprite, camera, corner_x, corner_y).hit);
    /* and almost nothing reaches 1 */
    CHECK(wgr_sprite3d_set_pick_alpha_test(sprite, true, 1.0f));
    CHECK(!wgr_pick_object(sprite, camera, corner_x, corner_y).hit);

    /* turning it off brings the whole quad back */
    CHECK(wgr_sprite3d_set_pick_alpha_test(sprite, false, 0.5f));
    CHECK(wgr_pick_object(sprite, camera, corner_x, corner_y).hit);

    /* a texture made from pixels has no file to re-read, so it keeps a mask when it
       has any transparency at all: a clear texture rejects every pick */
    static const unsigned char clear[4] = {255, 255, 255, 0};
    const wgr_handle_t clear_texture = wgri_texture_create_rgba(clear, 1, 1);
    CHECK(clear_texture != 0);
    CHECK(wgri_texture_get_alpha_mask(clear_texture, &mask, &width, &height));
    const wgr_handle_t clear_sprite = wgr_sprite3d_create(clear_texture);
    wgr_sprite3d_set_transform(clear_sprite, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(wgr_pick_object(clear_sprite, camera, middle, middle).hit); /* until it's asked */
    CHECK(wgr_sprite3d_set_pick_alpha_test(clear_sprite, true, 0.5f));
    CHECK(!wgr_pick_object(clear_sprite, camera, middle, middle).hit);

    /* an opaque one keeps none: there is nothing for the alpha test to reject */
    static const unsigned char opaque[4] = {255, 255, 255, 255};
    const wgr_handle_t opaque_texture = wgri_texture_create_rgba(opaque, 1, 1);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* the mask it hasn't got warns on purpose */
    CHECK(!wgri_texture_ensure_alpha_mask(opaque_texture));
    CHECK(!wgri_texture_get_alpha_mask(opaque_texture, &mask, &width, &height));
    const wgr_handle_t plain = wgr_sprite3d_create(opaque_texture);
    wgr_sprite3d_set_transform(plain, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    CHECK(wgr_sprite3d_set_pick_alpha_test(plain, true, 0.5f));
    CHECK(wgr_pick_object(plain, camera, middle, middle).hit);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);

    wgr_sprite3d_destroy(plain);
    wgr_sprite3d_destroy(clear_sprite);
    wgr_texture_release(opaque_texture);
    wgr_texture_release(clear_texture);
    wgr_sprite3d_destroy(sprite);
    wgr_texture_release(texture);
    wgri_sprite3d_deinit();
    wgri_material_deinit();
    wgri_texture_deinit();
    wgri_camera3d_deinit();
    wgri_scene_deinit();
    wgri_render_deinit();
    wgri_platform_set_window_size(was_width, was_height);
    sg_shutdown();
}
