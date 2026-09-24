#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr_logger.h"
#include "wgr_render.h"
#include "wgr_texture.h"
#include "wgr_window.h"
#include "internal/wgr_font_internal.h"
#include "wgr_color.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define EPS 1e-4f

static sg_view binding(wgr_handle_t texture)
{
    sg_view view = {0};
    wgri_texture_get_binding(texture, &view, NULL, NULL, NULL);
    return view;
}

/* Render target bookkeeping on sokol's dummy backend (no GPU): creation, the
 * pass being recorded, sizes, nesting, and refusing to sample a target inside
 * its own pass. The replay itself runs in the smoke test (examples/render_target.c). */
void test_render_targets(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_texture_init();
    wgri_render_init();
    wgri_font_init(); /* the text layer's built-in font lives there */
    wgri_text_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_FATAL); /* invalid calls below log on purpose */

    wgr_handle_t target = wgr_texture_create_target(64, 32);
    wgr_handle_t other = wgr_texture_create_target(16, 16);
    CHECK(target != 0 && other != 0);
    CHECK(wgr_texture_create_target(0, 32) == 0);
    CHECK(wgr_texture_create_target(64, -1) == 0);
    CHECK_NEAR(wgr_texture_get_size(target).x, 64, EPS);
    CHECK_NEAR(wgr_texture_get_size(target).y, 32, EPS);
    CHECK(!wgri_texture_is_flipped(wgr_texture_get_default())); /* loaded images are never flipped */
    CHECK(wgri_texture_is_flipped(target) == !sg_query_features().origin_top_left);

    CHECK(wgri_render_current_pass() == 0);
    CHECK(!wgr_render_begin_texture(wgr_texture_get_default())); /* not a render target */
    CHECK(!wgr_render_begin_texture(0));
    CHECK(wgri_render_current_pass() == 0);

    CHECK(wgr_render_begin_texture(target));
    CHECK(wgri_render_current_pass() == 1);
    CHECK_NEAR(wgri_render_target_size().x, 64, EPS);
    CHECK_NEAR(wgri_render_target_size().y, 32, EPS);
    CHECK(!wgr_render_begin_texture(other)); /* no nesting */
    CHECK(wgri_render_current_pass() == 1);
    /* a target can't be sampled inside its own pass: it binds as the default texture */
    CHECK(binding(target).id == binding(wgr_texture_get_default()).id);
    CHECK(binding(other).id != binding(wgr_texture_get_default()).id); /* other targets are fine */
    wgr_render_end_texture();

    CHECK(wgri_render_current_pass() == 0);
    CHECK(binding(target).id != binding(wgr_texture_get_default()).id);
    wgr_render_end_texture(); /* not drawing into a texture: ignored */
    CHECK(wgri_render_current_pass() == 0);

    CHECK(wgr_render_begin_texture(other)); /* passes are numbered in the order begun */
    CHECK(wgri_render_current_pass() == 2);
    CHECK_NEAR(wgri_render_target_size().x, 16, EPS);
    wgr_render_end_texture();

    /* sampling */
    CHECK(wgr_texture_set_sampling(target, WGR_TEXTURE_WRAP_REPEAT, WGR_TEXTURE_WRAP_MIRROR, WGR_TEXTURE_FILTER_NEAREST));
    CHECK(!wgr_texture_set_sampling(target, (wgr_texture_wrap_t)9, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_LINEAR));
    CHECK(!wgr_texture_set_sampling(0, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_LINEAR));

    wgr_texture_release(target);
    wgr_texture_release(other);
    CHECK(!wgr_render_begin_texture(target)); /* destroyed */

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_text_deinit();
    wgri_font_deinit();
    wgri_render_deinit();
    wgri_texture_deinit();
    sg_shutdown();
}

/* The clip stack (docs/PLAN-ui.md): pushes intersect with their parent, pops restore
 * it, a render target's pass starts unclipped, and a frame's unmatched pushes are
 * dropped at its end. */
void test_render_clip_stack(void)
{
    const vec2_t screen = wgr_window_get_screen_size();
    float x, y, w, h;

    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_texture_init();
    wgri_render_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR); /* the mismatches below warn */
    CHECK(wgr_window_set_size(800, 600));
    const wgr_handle_t target = wgr_texture_create_target(64, 32);

    wgr_render_begin_frame();
    CHECK(!wgri_render_get_clip(&x, &y, &w, &h)); /* nothing pushed: the whole screen */
    CHECK(x == 0 && y == 0 && w == 800 && h == 600);

    wgr_render_push_clip(100, 100, 300, 200);
    CHECK(wgri_render_get_clip(&x, &y, &w, &h));
    CHECK(x == 100 && y == 100 && w == 300 && h == 200);
    wgr_render_push_clip(50, 150, 200, 400); /* sticks out left and below: intersected */
    wgri_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 100 && y == 150 && w == 150 && h == 150);
    wgr_render_push_clip(500, 500, 10, 10); /* entirely outside its parent: empty */
    wgri_render_get_clip(&x, &y, &w, &h);
    CHECK(w == 0 && h == 0);
    wgr_render_pop_clip();
    wgr_render_pop_clip();
    wgri_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 100 && y == 100 && w == 300 && h == 200); /* back to the outer one */

    /* a render target's pass starts from its own whole target */
    CHECK(wgr_render_begin_texture(target));
    CHECK(!wgri_render_get_clip(&x, &y, &w, &h));
    CHECK(x == 0 && y == 0 && w == 64 && h == 32);
    wgr_render_push_clip(10, -5, 100, 20); /* clamped to the target, not the screen clip */
    wgri_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 10 && y == 0 && w == 54 && h == 15);
    wgr_render_end_texture(); /* left one push open: dropped */
    CHECK(wgri_render_get_clip(&x, &y, &w, &h)); /* the screen's clip applies again */
    CHECK(x == 100 && y == 100 && w == 300 && h == 200);

    wgr_render_pop_clip();
    CHECK(!wgri_render_get_clip(&x, &y, &w, &h));
    wgr_render_pop_clip(); /* one too many: ignored */
    CHECK(!wgri_render_get_clip(&x, &y, &w, &h));

    /* past the 32-deep limit, extra pushes don't clip but still pair with pops */
    for (int i = 0; i < 40; i++) wgr_render_push_clip((float)i, 0, 800, 600);
    wgri_render_get_clip(&x, &y, &w, &h);
    CHECK(x == 31);
    for (int i = 0; i < 39; i++) wgr_render_pop_clip();
    CHECK(wgri_render_get_clip(&x, &y, &w, &h) && x == 0);

    wgr_render_end_frame(); /* one push left open: dropped at the end of the frame */
    wgr_render_begin_frame();
    CHECK(!wgri_render_get_clip(&x, &y, &w, &h));
    wgr_render_end_frame();

    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_texture_release(target);
    wgri_render_deinit();
    wgri_texture_deinit();
    sg_shutdown();
}

/* Immediate textured draws with a source rectangle and nine-slice (docs/PLAN-ui.md),
 * counted in sokol_gl vertices on the dummy backend: 6 per quad. */
void test_texture_draw_immediate(void)
{
    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_texture_init();
    wgri_render_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    const wgr_handle_t texture = wgr_texture_create_target(64, 32);

    wgr_render_begin_frame();
    int before = sgl_num_vertices();
    wgr_texture_draw_ex(texture, 16, 0, 16, 16, 10.5f, 10.5f, 40, 40, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);

    before = sgl_num_vertices(); /* 4 px borders, drawn larger: nine patches */
    wgr_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 4, 4, 4, 0, 0, 200, 100, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 9 * 6);

    before = sgl_num_vertices(); /* sliced on one axis only: three patches */
    wgr_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 0, 4, 0, 0, 0, 200, 100, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 3 * 6);

    before = sgl_num_vertices(); /* no borders: the plain region */
    wgr_texture_draw_nine_slice(texture, 0, 0, 32, 32, 0, 0, 0, 0, 0, 0, 200, 100, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);

    before = sgl_num_vertices(); /* nothing to draw */
    wgr_texture_draw_nine_slice(texture, 0, 0, 32, 32, 4, 4, 4, 4, 0, 0, 0, 100, WGR_COLOR_WHITE);
    wgr_texture_draw_ex(0, 0, 0, 0, 0, 0, 0, 10, 10, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() == before);

    before = sgl_num_vertices(); /* the old call still draws the whole texture */
    wgr_texture_draw(texture, 0, 0, 0, 0, WGR_COLOR_WHITE);
    CHECK(sgl_num_vertices() - before == 6);
    wgr_render_end_frame();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_texture_release(texture);
    wgri_render_deinit();
    wgri_texture_deinit();
    sg_shutdown();
}

/* Draw `vertices` vertices in one draw, or `commands` draws each with its own matrix. */
static void record(int vertices, int commands)
{
    sgl_begin_triangles();
    for (int i = 0; i < vertices; i++) {
        sgl_v2f((float)(i % 3), (float)(i % 2));
    }
    sgl_end();
    for (int i = 0; i < commands; i++) {
        sgl_translate(1.0f, 0.0f, 0.0f);
        sgl_begin_triangles();
        sgl_v2f(0, 0);
        sgl_v2f(1, 0);
        sgl_v2f(0, 1);
        sgl_end();
    }
}

/* A frame that runs out of sokol_gl's vertex or command budget loses the draws past
 * it; the budget doubles for the frames after, so the same frame then fits. */
void test_render_sgl_growth(void)
{
    enum { VERTICES = 70000, COMMANDS = 20000 }; /* past the initial 65536 and 16384 */
    sgl_error_t err;

    sg_setup(&(sg_desc){.environment = wgri_platform_environment()});
    wgri_texture_init();
    wgri_render_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR); /* growing warns */

    wgr_render_begin_frame();
    record(100, 10);
    CHECK(!sgl_error().any);
    wgr_render_end_frame();

    wgr_render_begin_frame();
    record(VERTICES, 0);
    CHECK(sgl_error().vertices_full);
    wgr_render_end_frame();
    wgr_render_begin_frame();
    record(VERTICES, 0);
    err = sgl_error();
    CHECK(!err.any);
    CHECK(sgl_num_vertices() >= VERTICES);
    wgr_render_end_frame();

    wgr_render_begin_frame();
    record(0, COMMANDS);
    err = sgl_error();
    CHECK(err.commands_full || err.uniforms_full);
    wgr_render_end_frame();
    wgr_render_begin_frame();
    record(0, COMMANDS);
    err = sgl_error();
    CHECK(!err.any);
    CHECK(sgl_num_commands() >= COMMANDS);
    wgr_render_end_frame();

    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgri_render_deinit();
    wgri_texture_deinit();
    sg_shutdown();
}
