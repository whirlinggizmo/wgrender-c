#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"
#include "sk_render.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

#define EPS 1e-4f

static sg_view binding(sk_handle_t texture)
{
    sg_view view = {0};
    sk_texture_get_binding(texture, &view, NULL, NULL, NULL);
    return view;
}

/* Render target bookkeeping on sokol's dummy backend (no GPU): creation, the
 * pass being recorded, sizes, nesting, and refusing to sample a target inside
 * its own pass. The replay itself runs in `make smoke` (examples/render_target.c). */
void test_render_targets(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_color_init();
    sk_texture_init();
    sk_render_init();
    sk_text_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_FATAL); /* invalid calls below log on purpose */

    sk_handle_t target = sk_texture_create_target(64, 32);
    sk_handle_t other = sk_texture_create_target(16, 16);
    CHECK(target != 0 && other != 0);
    CHECK(sk_texture_create_target(0, 32) == 0);
    CHECK(sk_texture_create_target(64, -1) == 0);
    CHECK_NEAR(sk_texture_get_size(target).x, 64, EPS);
    CHECK_NEAR(sk_texture_get_size(target).y, 32, EPS);
    CHECK(!sk_texture_is_flipped(sk_texture_get_default())); /* loaded images are never flipped */
    CHECK(sk_texture_is_flipped(target) == !sg_query_features().origin_top_left);

    CHECK(sk_render_current_pass() == 0);
    CHECK(!sk_render_begin_texture(sk_texture_get_default())); /* not a render target */
    CHECK(!sk_render_begin_texture(0));
    CHECK(sk_render_current_pass() == 0);

    CHECK(sk_render_begin_texture(target));
    CHECK(sk_render_current_pass() == 1);
    CHECK_NEAR(sk_render_target_size().x, 64, EPS);
    CHECK_NEAR(sk_render_target_size().y, 32, EPS);
    CHECK(!sk_render_begin_texture(other)); /* no nesting */
    CHECK(sk_render_current_pass() == 1);
    /* a target can't be sampled inside its own pass: it binds as the default texture */
    CHECK(binding(target).id == binding(sk_texture_get_default()).id);
    CHECK(binding(other).id != binding(sk_texture_get_default()).id); /* other targets are fine */
    sk_render_end_texture();

    CHECK(sk_render_current_pass() == 0);
    CHECK(binding(target).id != binding(sk_texture_get_default()).id);
    sk_render_end_texture(); /* not drawing into a texture: ignored */
    CHECK(sk_render_current_pass() == 0);

    CHECK(sk_render_begin_texture(other)); /* passes are numbered in the order begun */
    CHECK(sk_render_current_pass() == 2);
    CHECK_NEAR(sk_render_target_size().x, 16, EPS);
    sk_render_end_texture();

    /* sampling */
    CHECK(sk_texture_set_sampling(target, SK_TEXTURE_WRAP_REPEAT, SK_TEXTURE_WRAP_MIRROR, SK_TEXTURE_FILTER_NEAREST));
    CHECK(!sk_texture_set_sampling(target, (sk_texture_wrap_t)9, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_LINEAR));
    CHECK(!sk_texture_set_sampling(0, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_LINEAR));

    sk_texture_destroy(target);
    sk_texture_destroy(other);
    CHECK(!sk_render_begin_texture(target)); /* destroyed */

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_text_deinit();
    sk_render_deinit();
    sk_texture_deinit();
    sk_color_deinit();
    sg_shutdown();
}
