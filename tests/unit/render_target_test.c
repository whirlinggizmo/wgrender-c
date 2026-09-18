#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"
#include "sk_render.h"
#include "sk_texture.h"
#include "sk_window.h"
#include "internal/sk_font.h"
#include "sk_color.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

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
    sk_texture_init();
    sk_render_init();
    sk_font_init(); /* the text layer's built-in font lives there */
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

    sk_texture_release(target);
    sk_texture_release(other);
    CHECK(!sk_render_begin_texture(target)); /* destroyed */

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_text_deinit();
    sk_font_deinit();
    sk_render_deinit();
    sk_texture_deinit();
    sg_shutdown();
}

/* The clip stack (docs/PLAN-ui.md): pushes intersect with their parent, pops restore
 * it, a render target's pass starts unclipped, and a frame's unmatched pushes are
 * dropped at its end. */
void test_render_clip_stack(void)
{
    const vec2_t screen = sk_window_get_screen_size();
    float x, y, w, h;

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_texture_init();
    sk_render_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR); /* the mismatches below warn */
    CHECK(sk_window_set_size(800, 600));
    const sk_handle_t target = sk_texture_create_target(64, 32);

    sk_render_begin();
    CHECK(!sk_render_get_clip(&x, &y, &w, &h)); /* nothing pushed: the whole screen */
    CHECK(x == 0 && y == 0 && w == 800 && h == 600);

    sk_render_push_clip(100, 100, 300, 200);
    CHECK(sk_render_get_clip(&x, &y, &w, &h));
    CHECK(x == 100 && y == 100 && w == 300 && h == 200);
    sk_render_push_clip(50, 150, 200, 400); /* sticks out left and below: intersected */
    sk_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 100 && y == 150 && w == 150 && h == 150);
    sk_render_push_clip(500, 500, 10, 10); /* entirely outside its parent: empty */
    sk_render_get_clip(&x, &y, &w, &h);
    CHECK(w == 0 && h == 0);
    sk_render_pop_clip();
    sk_render_pop_clip();
    sk_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 100 && y == 100 && w == 300 && h == 200); /* back to the outer one */

    /* a render target's pass starts from its own whole target */
    CHECK(sk_render_begin_texture(target));
    CHECK(!sk_render_get_clip(&x, &y, &w, &h));
    CHECK(x == 0 && y == 0 && w == 64 && h == 32);
    sk_render_push_clip(10, -5, 100, 20); /* clamped to the target, not the screen clip */
    sk_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 10 && y == 0 && w == 54 && h == 15);
    sk_render_end_texture(); /* left one push open: dropped */
    CHECK(sk_render_get_clip(&x, &y, &w, &h)); /* the screen's clip applies again */
    CHECK(x == 100 && y == 100 && w == 300 && h == 200);

    sk_render_pop_clip();
    CHECK(!sk_render_get_clip(&x, &y, &w, &h));
    sk_render_pop_clip(); /* one too many: ignored */
    CHECK(!sk_render_get_clip(&x, &y, &w, &h));

    /* past the 32-deep limit, extra pushes don't clip but still pair with pops */
    for (int i = 0; i < 40; i++) sk_render_push_clip((float)i, 0, 800, 600);
    sk_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 31);
    for (int i = 0; i < 39; i++) sk_render_pop_clip();
    CHECK(sk_render_get_clip(&x, &y, &w, &h) && x == 0);

    sk_render_end(); /* one push left open: dropped at the end of the frame */
    sk_render_begin();
    CHECK(!sk_render_get_clip(&x, &y, &w, &h));
    sk_render_end();

    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_texture_release(target);
    sk_render_deinit();
    sk_texture_deinit();
    sg_shutdown();
}

/* Immediate textured draws with a source rectangle and nine-slice (docs/PLAN-ui.md),
 * counted in sokol_gl vertices on the dummy backend: 6 per quad. */
void test_texture_draw_immediate(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_texture_init();
    sk_render_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    const sk_handle_t texture = sk_texture_create_target(64, 32);

    sk_render_begin();
    int before = sgl_num_vertices();
    sk_texture_draw_ex(texture, 16, 0, 16, 16, 10.5f, 10.5f, 40, 40, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);

    before = sgl_num_vertices(); /* 4 px borders, drawn larger: nine patches */
    sk_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 4, 4, 4, 0, 0, 200, 100, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 9 * 6);

    before = sgl_num_vertices(); /* sliced on one axis only: three patches */
    sk_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 0, 4, 0, 0, 0, 200, 100, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 3 * 6);

    before = sgl_num_vertices(); /* no borders: the plain region */
    sk_texture_draw_nine_slice(texture, 0, 0, 32, 32, 0, 0, 0, 0, 0, 0, 200, 100, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);

    before = sgl_num_vertices(); /* nothing to draw */
    sk_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 4, 4, 4, 0, 0, 0, 100, SK_COLOR_WHITE);
    sk_texture_draw_ex(0, 0, 0, 0, 0, 0, 0, 10, 10, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() == before);

    before = sgl_num_vertices(); /* the old call still draws the whole texture */
    sk_texture_draw(texture, 0, 0, 0, 0, SK_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);
    sk_render_end();

    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_texture_release(texture);
    sk_render_deinit();
    sk_texture_deinit();
    sg_shutdown();
}
