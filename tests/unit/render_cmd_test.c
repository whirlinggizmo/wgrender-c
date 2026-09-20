/* The frame command list (src/sk_render.c): sokol_gl layers, model runs, sprite
 * batches and callbacks are recorded in call order and replayed at sk_render_end. What
 * matters for batching is what it merges — model runs that follow each other with
 * nothing drawn in between become one command, and a layer nothing was drawn into is
 * dropped rather than replayed — and what it must not merge: runs in different passes,
 * or runs with drawing between them. */
#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_texture.h"
#include "sk_color.h"
#include "sk_render.h"
#include "sk_shape2d.h"
#include "sk_texture.h"
#include "test.h"
#include "tests.h"

#include "sokol_gfx.h"

static int drawn; /* callbacks the replay ran */

static void count_draw(int arg)
{
    drawn += arg;
}

void test_render_command_merging(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();

    sk_render_begin();
    const int start = sk_render_command_count();
    CHECK(start >= 1); /* a layer is open, waiting for 2D drawing */

    /* a model run, and the layer opened after it for whatever is drawn next. The
       layer that was open stays: it's the frame's first command, so dropping it would
       leave the list empty */
    sk_render_submit_models(0, 2);
    const int after_first = sk_render_command_count();
    CHECK(after_first == start + 2);

    /* the next run carries on where it left off, with nothing drawn in between: the
       empty layer between them goes, and the runs become one command */
    sk_render_submit_models(2, 3);
    CHECK(sk_render_command_count() == after_first);
    sk_render_submit_models(5, 1);
    CHECK(sk_render_command_count() == after_first);

    /* a run that doesn't carry on is its own command, and costs one: the empty layer
       between the two runs is dropped, and a fresh one opened after */
    sk_render_submit_models(20, 1);
    CHECK(sk_render_command_count() == after_first + 1);

    /* nothing to submit changes nothing */
    sk_render_submit_models(21, 0);
    sk_render_submit_models(21, -3);
    CHECK(sk_render_command_count() == after_first + 1);

    /* 2D drawing between two runs keeps them apart, whether they'd carry on or not:
       the layer it went into has to be replayed between them */
    sk_shape2d_draw_rectangle(1, 1, 10, 10, SK_COLOR_WHITE);
    const int after_2d = sk_render_command_count();
    sk_render_submit_models(21, 1);
    CHECK(sk_render_command_count() == after_2d + 2);
    sk_render_end();

    /* a sprite batch is open only while its command is the last one recorded */
    sk_render_begin();
    CHECK(!sk_render_sprites_open(3));
    CHECK(sk_render_submit_sprites(3));
    CHECK(sk_render_sprites_open(3));
    CHECK(!sk_render_sprites_open(4)); /* a different batch isn't open */
    sk_shape2d_draw_rectangle(1, 1, 10, 10, SK_COLOR_WHITE);
    CHECK(!sk_render_sprites_open(3)); /* something was drawn over it */
    CHECK(sk_render_submit_sprites(3));
    CHECK(sk_render_sprites_open(3));
    sk_render_submit_models(0, 1);
    CHECK(!sk_render_sprites_open(3));
    sk_render_end();

    /* callbacks are recorded in order and run at replay, each exactly once */
    drawn = 0;
    sk_render_begin();
    sk_render_submit_callback(count_draw, 1);
    sk_render_submit_callback(count_draw, 10);
    sk_render_submit_callback(NULL, 100); /* nothing to run */
    sk_render_end();
    CHECK(drawn == 11);

    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}

/* Model runs in different passes never merge: each pass replays only its own
 * commands, so a run recorded while drawing into a texture must stay in that pass. */
void test_render_command_passes(void)
{
    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();

    const sk_handle_t target = sk_texture_create_target(32, 32);
    CHECK(target != 0);

    sk_render_begin();
    sk_render_submit_models(0, 2);
    CHECK(sk_render_current_pass() == 0);

    CHECK(sk_render_begin_texture(target));
    CHECK(sk_render_current_pass() == 1);
    const int in_target = sk_render_command_count();
    sk_render_submit_models(2, 2); /* would carry on, but it's another pass */
    CHECK(sk_render_command_count() == in_target + 1);
    sk_render_submit_models(4, 1); /* and inside the pass they do merge */
    CHECK(sk_render_command_count() == in_target + 1);
    sk_render_end_texture();
    CHECK(sk_render_current_pass() == 0);

    /* back on the screen, the run before the target doesn't take it back either. It
       costs two: ending the target opens a layer and puts the screen's clip back into
       it, so that layer has something in it and is replayed rather than dropped */
    const int back = sk_render_command_count();
    sk_render_submit_models(5, 1);
    CHECK(sk_render_command_count() == back + 2);
    sk_render_end();

    sk_texture_release(target);
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
