#include "sk_render.h"

#include <stdbool.h>
#include <stdlib.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_module.h"
#include "internal/sk_platform.h"
#include "internal/sk_environment.h"
#include "internal/sk_render.h"

/* internal/sk_environment.h: the environment module fills this in when it's linked. */
sk_environment_hooks_t sk_environment_hooks;
#include "sk_camera3d.h"
#include "sk_logger.h"
#include "sk_window.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

sk_render_hooks_t sk_render_hooks;

/* Render targets are textures (sk_texture, through its hooks): false when `texture`
 * isn't one, or textures aren't linked. */
static bool target_of(sk_handle_t texture, sg_attachments *attachments, int *w, int *h)
{
    return sk_render_hooks.texture_target != NULL && sk_render_hooks.texture_target(texture, attachments, w, h);
}

static void drawing_into(sk_handle_t texture)
{
    if (sk_render_hooks.texture_drawing_into != NULL) sk_render_hooks.texture_drawing_into(texture);
}

/* sokol_gl's per-frame budgets, shared by everything drawn through it in a frame:
 * sprites, 2D and 3D shapes, text. They start at SK_SGL_VERTICES / SK_SGL_COMMANDS
 * (sokol's defaults) and double after a frame that ran out, up to SK_SGL_MAX_*; all
 * overridable at build time. A frame that runs out loses the draws that didn't fit. */
#ifndef SK_SGL_VERTICES
#define SK_SGL_VERTICES 65536
#endif
#ifndef SK_SGL_COMMANDS
#define SK_SGL_COMMANDS 16384
#endif
#ifndef SK_SGL_MAX_VERTICES
#define SK_SGL_MAX_VERTICES (1 << 20) /* 24 MB */
#endif
#ifndef SK_SGL_MAX_COMMANDS
#define SK_SGL_MAX_COMMANDS (1 << 18)
#endif

/* The frame's command list starts at RENDER_CMDS_INITIAL and doubles as needed, up
 * to SK_MAX_RENDER_CMDS (overridable at build time). Every switch between sokol_gl
 * drawing and models, sprites or a callback adds one. */
#define RENDER_CMDS_INITIAL 256
#ifndef SK_MAX_RENDER_CMDS
#define SK_MAX_RENDER_CMDS (1 << 20)
#endif
#define MAX_RENDER_PASSES 17 /* the screen + 16 render target passes per frame */

/* Render model
 * -----------
 * sokol_gl records draw commands into internal buffers during the frame and
 * replays them inside a live sg pass. raylib clears at BeginDrawing(); sokol
 * clears via the pass load-action. To keep the librl-style
 * begin/clear/draw/end ordering, we record everything between sk_render_begin()
 * and sk_render_end(), then open the swapchain pass in sk_render_end() (where
 * the clear color is already known) and replay the frame's command list (sgl
 * layers, including text, and model draws, in call order; see internal/sk_render.h).
 */

typedef enum {
    RENDER_CMD_SGL_LAYER,
    RENDER_CMD_MODELS,
    RENDER_CMD_CALLBACK,
    RENDER_CMD_SPRITES,
} sk_render_cmd_kind_t;

typedef struct {
    sk_render_cmd_kind_t kind;
    int pass;  /* index into sk_render_passes */
    int layer; /* RENDER_CMD_SGL_LAYER */
    int first; /* RENDER_CMD_MODELS: item range in sk_model's queue; RENDER_CMD_SGL_LAYER:
                  the layer's sokol_gl command range; RENDER_CMD_SPRITES: the batch */
    int count;
    sk_render_callback_fn callback; /* RENDER_CMD_CALLBACK: called with `first` */
} sk_render_cmd_t;

/* A render pass recorded this frame: pass 0 is the screen, the rest are render
 * targets in the order they were begun. */
typedef struct {
    sk_handle_t target; /* 0 = the screen */
    sk_colorf_t clear_color;
} sk_render_pass_t;

static sk_render_pass_t sk_render_passes[MAX_RENDER_PASSES];
static int sk_render_pass_count;
static int sk_render_current_pass_index; /* pass being recorded */
static bool sk_render_pass_overflow_logged;
static sgl_pipeline sk_pip_2d;
static sgl_pipeline sk_pip_3d;
static sgl_pipeline sk_pip_3d_transparent;

static sk_render_cmd_t *sk_render_cmds;
static int sk_render_cmd_capacity;
static int sk_render_cmd_count;
static int sk_render_next_layer;
/* sokol_gl totals when the current layer was opened, to detect empty layers */
static int sk_layer_mark_vertices;
static int sk_layer_mark_commands;
static bool sk_render_overflow_logged;
static unsigned sk_render_revision; /* bumped when the pass, 3D mode or clip changes */
/* the sokol_gl context everything records into, and its budgets */
static sgl_context sk_sgl_ctx;
static int sk_sgl_vertices;
static int sk_sgl_commands;
static bool sk_sgl_at_most_logged;

/* Room for `more` commands, growing the list; false at the most there can be (logged
 * once: what doesn't fit is drawn out of order or dropped). */
static bool room_for(int more)
{
    int capacity = sk_render_cmd_capacity > 0 ? sk_render_cmd_capacity : RENDER_CMDS_INITIAL;
    sk_render_cmd_t *grown;
    if (sk_render_cmd_count + more <= sk_render_cmd_capacity) {
        return true;
    }
    while (capacity < sk_render_cmd_count + more && capacity < SK_MAX_RENDER_CMDS) capacity *= 2;
    if (capacity > SK_MAX_RENDER_CMDS) capacity = SK_MAX_RENDER_CMDS;
    if (sk_render_cmd_count + more > capacity ||
        (grown = realloc(sk_render_cmds, sizeof(*grown) * (size_t)capacity)) == NULL) {
        if (!sk_render_overflow_logged) {
            log_warn("render: %d render commands in a frame, the most there can be; the rest may be drawn out of "
                     "order or dropped",
                     sk_render_cmd_count);
            sk_render_overflow_logged = true;
        }
        return false;
    }
    sk_render_cmds = grown;
    sk_render_cmd_capacity = capacity;
    return true;
}

static void open_sgl_layer(void)
{
    int layer;
    if (!room_for(1)) {
        return; /* keep drawing into the current layer */
    }
    layer = sk_render_next_layer++;
    sgl_layer(layer);
    sk_layer_mark_vertices = sgl_num_vertices();
    sk_layer_mark_commands = sgl_num_commands();
    sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
        .kind = RENDER_CMD_SGL_LAYER,
        .pass = sk_render_current_pass_index,
        .layer = layer,
        .first = sk_layer_mark_commands, /* its sokol_gl commands start here; count: count_layer_commands */
    };
}

static void reset_frame_commands(void)
{
    sk_render_revision++;
    const sk_colorf_t screen_clear = sk_render_passes[0].clear_color;
    sk_render_cmd_count = 0;
    sk_render_next_layer = 0;
    sk_render_pass_count = 1;
    sk_render_current_pass_index = 0;
    sk_render_passes[0] = (sk_render_pass_t){.target = 0, .clear_color = screen_clear}; /* the screen keeps its clear color */
    drawing_into(0);
    open_sgl_layer();
}

int sk_render_current_pass(void)
{
    return sk_render_current_pass_index;
}

vec2_t sk_render_target_size(void)
{
    const sk_handle_t target = sk_render_passes[sk_render_current_pass_index].target;
    sg_attachments attachments;
    int w = 0, h = 0;
    if (target != 0 && target_of(target, &attachments, &w, &h)) {
        return (vec2_t){(float)w, (float)h};
    }
    return (vec2_t){(float)sk_platform_width(), (float)sk_platform_height()};
}

void sk_render_submit_models(int first, int count)
{
    sk_render_cmd_t *last;

    if (count <= 0) {
        return;
    }

    /* drop the current sgl layer if nothing was recorded into it */
    last = &sk_render_cmds[sk_render_cmd_count - 1];
    if (sk_render_cmd_count > 1 && last->kind == RENDER_CMD_SGL_LAYER && last->pass == sk_render_current_pass_index &&
        sgl_num_vertices() == sk_layer_mark_vertices &&
        sgl_num_commands() == sk_layer_mark_commands) {
        sk_render_cmd_count--;
        sk_render_next_layer--;
        last = &sk_render_cmds[sk_render_cmd_count - 1];
    }

    if (last->kind == RENDER_CMD_MODELS && last->pass == sk_render_current_pass_index &&
        last->first + last->count == first) {
        last->count += count; /* extend the adjacent model run */
    } else if (room_for(2)) { /* the run, and the sgl layer after it */
        sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
            .kind = RENDER_CMD_MODELS,
            .pass = sk_render_current_pass_index,
            .first = first,
            .count = count,
        };
    } else {
        /* out of commands (logged): fold into the last model run (order may be off) */
        for (int i = sk_render_cmd_count - 1; i >= 0; i--) {
            if (sk_render_cmds[i].kind == RENDER_CMD_MODELS && sk_render_cmds[i].pass == sk_render_current_pass_index) {
                sk_render_cmds[i].count = first + count - sk_render_cmds[i].first;
                return;
            }
        }
        return;
    }
    open_sgl_layer();
}

void sk_render_submit_callback(sk_render_callback_fn draw, int arg)
{
    sk_render_cmd_t *last;

    if (draw == NULL || !room_for(2)) {
        return;
    }
    last = &sk_render_cmds[sk_render_cmd_count - 1];
    /* drop the current sgl layer if nothing was recorded into it */
    if (sk_render_cmd_count > 1 && last->kind == RENDER_CMD_SGL_LAYER && last->pass == sk_render_current_pass_index &&
        sgl_num_vertices() == sk_layer_mark_vertices && sgl_num_commands() == sk_layer_mark_commands) {
        sk_render_cmd_count--;
        sk_render_next_layer--;
    }
    sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
        .kind = RENDER_CMD_CALLBACK,
        .pass = sk_render_current_pass_index,
        .first = arg,
        .callback = draw,
    };
    open_sgl_layer();
}

/* Whether the current sgl layer has had nothing recorded into it since it opened. */
static bool layer_is_empty(void)
{
    return sgl_num_vertices() == sk_layer_mark_vertices && sgl_num_commands() == sk_layer_mark_commands;
}

bool sk_render_submit_sprites(int batch)
{
    sk_render_cmd_t *last;

    if (!room_for(2)) { /* the batch, and the sgl layer after it */
        return false;
    }
    last = &sk_render_cmds[sk_render_cmd_count - 1];
    /* drop the current sgl layer if nothing was recorded into it */
    if (sk_render_cmd_count > 1 && last->kind == RENDER_CMD_SGL_LAYER && last->pass == sk_render_current_pass_index &&
        layer_is_empty()) {
        sk_render_cmd_count--;
        sk_render_next_layer--;
    }
    sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
        .kind = RENDER_CMD_SPRITES,
        .pass = sk_render_current_pass_index,
        .first = batch,
    };
    open_sgl_layer();
    return true;
}

bool sk_render_sprites_open(int batch)
{
    /* the batch's command, then only the (empty) sgl layer opened after it */
    return batch >= 0 && sk_render_cmd_count >= 2 && layer_is_empty() &&
           sk_render_cmds[sk_render_cmd_count - 1].kind == RENDER_CMD_SGL_LAYER &&
           sk_render_cmds[sk_render_cmd_count - 2].kind == RENDER_CMD_SPRITES &&
           sk_render_cmds[sk_render_cmd_count - 2].first == batch &&
           sk_render_cmds[sk_render_cmd_count - 2].pass == sk_render_current_pass_index;
}

static bool sk_render_transparent_3d;

void sk_render_set_3d_transparent(bool transparent)
{
    sgl_load_pipeline(transparent ? sk_pip_3d_transparent : sk_pip_3d);
    sk_render_transparent_3d = transparent;
}

bool sk_render_is_3d_transparent(void)
{
    return sk_render_transparent_3d;
}

void sk_render_init(void)
{
    /* sokol_gl's default context can't be resized or destroyed, so it stays minimal
     * and unused; recording goes into a context of our own that can be replaced
     * with a larger one */
    sgl_setup(&(sgl_desc_t){
        .max_vertices = 64,
        .max_commands = 16,
        .logger.func = 0,
    });
    sk_sgl_vertices = SK_SGL_VERTICES;
    sk_sgl_commands = SK_SGL_COMMANDS;
    sk_sgl_ctx = sgl_make_context(&(sgl_context_desc_t){
        .max_vertices = sk_sgl_vertices,
        .max_commands = sk_sgl_commands,
    });
    sgl_set_context(sk_sgl_ctx);

    /* alpha-blended pipeline for 2D primitives (no depth) */
    sk_pip_2d = sgl_make_pipeline(&(sg_pipeline_desc){
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
    });

    /* depth-tested pipeline for 3D primitives */
    sk_pip_3d = sgl_make_pipeline(&(sg_pipeline_desc){
        .depth = {
            .write_enabled = true,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
        },
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
    });

    /* transparent pass: depth-tested, blended, no depth writes */
    sk_pip_3d_transparent = sgl_make_pipeline(&(sg_pipeline_desc){
        .depth = {
            .write_enabled = false,
            .compare = SG_COMPAREFUNC_LESS_EQUAL,
        },
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
    });

    sk_render_passes[0].clear_color = (sk_colorf_t){0.1f, 0.1f, 0.1f, 1.0f};
    reset_frame_commands();
}

void sk_render_deinit(void)
{
    free(sk_render_cmds);
    sk_render_cmds = NULL;
    sk_render_cmd_capacity = 0;
    sk_render_cmd_count = 0;
    sgl_destroy_pipeline(sk_pip_2d);
    sgl_destroy_pipeline(sk_pip_3d);
    sgl_destroy_pipeline(sk_pip_3d_transparent);
    sgl_destroy_context(sk_sgl_ctx);
    sk_sgl_ctx = (sgl_context){0};
    sgl_shutdown();
}

static void setup_2d_projection(void)
{
    sk_render_revision++;
    /* the screen in logical pixels, a render target in its pixels */
    const vec2_t size = sk_render_current_pass_index == 0 ? sk_window_get_screen_size() : sk_render_target_size();
    const float w = size.x;
    const float h = size.y;

    sgl_defaults();
    sgl_load_pipeline(sk_pip_2d);
    sgl_matrix_mode_projection();
    sgl_load_identity();
    /* top-left origin, y down (raylib-style pixel space) */
    sgl_ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

SK_KEEP
void sk_render_begin(void)
{
    setup_2d_projection();
}

SK_KEEP
void sk_render_clear_background(sk_color_t color)
{
    sk_render_passes[sk_render_current_pass_index].clear_color = sk_color_unpack(color);
}

/* the clip stack, below */
static void clip_begin_pass(void);
static void clip_end_pass(void);
static void clip_end_frame(void);
static void apply_clip(void);

SK_KEEP
bool sk_render_begin_texture(sk_handle_t texture)
{
    sg_attachments attachments;
    int w = 0, h = 0;

    if (sk_render_current_pass_index != 0) {
        log_warn("sk_render_begin_texture: already drawing into a texture (call sk_render_end_texture first)");
        return false;
    }
    if (!target_of(texture, &attachments, &w, &h)) {
        log_warn("sk_render_begin_texture: not a render target texture (see sk_texture_create_target)");
        return false;
    }
    if (sk_render_pass_count >= MAX_RENDER_PASSES || !room_for(2)) {
        if (!sk_render_pass_overflow_logged) {
            log_warn("render: too many render target passes this frame (max %d)", MAX_RENDER_PASSES - 1);
            sk_render_pass_overflow_logged = true;
        }
        return false;
    }
    sk_render_passes[sk_render_pass_count] = (sk_render_pass_t){.target = texture};
    sk_render_current_pass_index = sk_render_pass_count++;
    clip_begin_pass();
    drawing_into(texture);
    open_sgl_layer();
    setup_2d_projection();
    return true;
}

SK_KEEP
void sk_render_end_texture(void)
{
    sk_render_revision++;
    if (sk_render_current_pass_index == 0) {
        log_warn("sk_render_end_texture: not drawing into a texture");
        return;
    }
    sk_render_current_pass_index = 0;
    drawing_into(0);
    clip_end_pass();
    open_sgl_layer();
    setup_2d_projection();
    apply_clip(); /* the screen's clip, in the layer that continues the screen pass */
}

/* Replay the commands recorded for pass `index` into the open sg pass. */
/* Each sokol_gl layer's commands run from where it opened to where the next one did
 * (layers open one after another, and sokol_gl never merges across them). */
static void count_layer_commands(void)
{
    sk_render_cmd_t *previous = NULL;
    for (int i = 0; i < sk_render_cmd_count; i++) {
        sk_render_cmd_t *cmd = &sk_render_cmds[i];
        if (cmd->kind != RENDER_CMD_SGL_LAYER) {
            continue;
        }
        if (previous != NULL) {
            previous->count = cmd->first - previous->first;
        }
        previous = cmd;
    }
    if (previous != NULL) {
        previous->count = sgl_num_commands() - previous->first;
    }
}

static void replay_pass(int index)
{
    bool previous_sprites = false;
    for (int i = 0; i < sk_render_cmd_count; i++) {
        const sk_render_cmd_t *cmd = &sk_render_cmds[i];
        if (cmd->pass != index) {
            continue;
        }
        if (cmd->kind == RENDER_CMD_SGL_LAYER) {
            /* shapes / 2D / fontstash text: only this layer's own commands, so replaying
               many layers stays linear */
            sgl_draw_layer_range(cmd->layer, cmd->first, cmd->count);
        } else if (cmd->kind == RENDER_CMD_MODELS) {
            if (sk_render_hooks.draw_models != NULL) { /* custom-pipeline meshes */
                sk_render_hooks.draw_models(cmd->first, cmd->count);
            }
        } else if (cmd->kind == RENDER_CMD_SPRITES) {
            if (sk_render_hooks.draw_sprites != NULL) { /* instanced sprite quads */
                sk_render_hooks.draw_sprites(cmd->first, previous_sprites);
            }
        } else {
            cmd->callback(cmd->first);
        }
        previous_sprites = cmd->kind == RENDER_CMD_SPRITES;
    }
}

/* Clear the pass, or keep what an earlier pass drew into the same target. */
static sg_pass_action pass_action(int index)
{
    sk_colorf_t color = sk_render_passes[index].clear_color;
    if (index == 0 && sk_platform_is_window_transparent()) {
        /* a transparent window composites premultiplied: clearing to (1, 0, 0, 0)
           would add red to what's behind it, so the clear color's alpha applies */
        color.r *= color.a, color.g *= color.a, color.b *= color.a;
    }
    bool drawn_before = false;
    for (int p = 1; p < index && !drawn_before; p++) {
        drawn_before = sk_render_passes[p].target == sk_render_passes[index].target;
    }
    return (sg_pass_action){
        .colors[0] = {
            .load_action = drawn_before ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR,
            .clear_value = {color.r, color.g, color.b, color.a},
        },
        .depth = {.load_action = drawn_before ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR, .clear_value = 1.0f},
    };
}

SK_KEEP
/* After a frame that ran out of sokol_gl's vertex or command budget (its draws past
 * the budget were dropped): switch to a context with that budget doubled, up to
 * SK_SGL_MAX_*. Pipelines carry over: they depend only on the pixel formats, which
 * every context here shares. */
static int doubled(int size, int most)
{
    return size < most / 2 ? size * 2 : most;
}

static void grow_sgl_budgets(sgl_error_t err)
{
    const bool vertices_full = err.vertices_full && sk_sgl_vertices < SK_SGL_MAX_VERTICES;
    const bool commands_full = (err.commands_full || err.uniforms_full) && sk_sgl_commands < SK_SGL_MAX_COMMANDS;
    const int vertices = vertices_full ? doubled(sk_sgl_vertices, SK_SGL_MAX_VERTICES) : sk_sgl_vertices;
    const int commands = commands_full ? doubled(sk_sgl_commands, SK_SGL_MAX_COMMANDS) : sk_sgl_commands;
    sgl_context ctx;

    if (!err.vertices_full && !err.commands_full && !err.uniforms_full) {
        return;
    }
    if (!vertices_full && !commands_full) {
        if (!sk_sgl_at_most_logged) {
            log_warn("render: a frame needed more than %d vertices or %d draw commands, the most there can be; "
                     "draws past them were dropped",
                     SK_SGL_MAX_VERTICES, SK_SGL_MAX_COMMANDS);
            sk_sgl_at_most_logged = true;
        }
        return;
    }
    ctx = sgl_make_context(&(sgl_context_desc_t){.max_vertices = vertices, .max_commands = commands});
    if (ctx.id == SG_INVALID_ID) {
        log_error("render: couldn't grow the draw budget to %d vertices, %d commands", vertices, commands);
        return;
    }
    log_warn("render: a frame ran out of %s (%d vertices, %d commands) and lost the draws past it; "
             "growing to %d vertices, %d commands",
             vertices_full && commands_full ? "vertices and draw commands"
             : vertices_full                ? "vertices"
                                            : "draw commands",
             sk_sgl_vertices, sk_sgl_commands, vertices, commands);
    sgl_destroy_context(sk_sgl_ctx);
    sk_sgl_ctx = ctx;
    sk_sgl_vertices = vertices;
    sk_sgl_commands = commands;
    sgl_set_context(ctx);
}

void sk_render_end(void)
{
    sgl_error_t sgl_err;
    if (sk_render_current_pass_index != 0) {
        log_warn("sk_render_end: still drawing into a texture (missing sk_render_end_texture)");
        sk_render_end_texture();
    }
    clip_end_frame();
    sk_debug_draw();

    /* upload the font atlas before opening the pass (sg_update_image cannot run
     * inside a render pass) */
    sk_font_flush();
    sk_module_flush_all(); /* the frame's sprite instances, particles, ... in one update each */
    count_layer_commands();

    /* render targets first, in the order they were begun, then the screen */
    for (int p = 1; p < sk_render_pass_count; p++) {
        sg_attachments attachments;
        int w = 0, h = 0;
        if (!target_of(sk_render_passes[p].target, &attachments, &w, &h)) {
            continue; /* destroyed during the frame */
        }
        drawing_into(sk_render_passes[p].target);
        sg_begin_pass(&(sg_pass){
            .action = pass_action(p),
            .attachments = attachments,
            .label = "sk-render-target",
        });
        replay_pass(p);
        sg_end_pass();
    }
    drawing_into(0);
    sg_begin_pass(&(sg_pass){
        .action = pass_action(0),
        .swapchain = sk_platform_swapchain(),
    });
    replay_pass(0);
    sg_end_pass();
    sgl_err = sgl_error(); /* sg_commit clears it */
    sg_commit();
    grow_sgl_budgets(sgl_err);

    sk_module_end_frame_all(); /* models, sprites, particles, lights, ... start over */
    sk_font_end_frame();
    reset_frame_commands();
}

SK_KEEP
void sk_render_begin_mode_2d(void)
{
    setup_2d_projection();
}

SK_KEEP
void sk_render_end_mode_2d(void)
{
    /* 2D is the default projection; nothing to restore for the milestone. */
}

/* ---- clipping ---------------------------------------------------------- */

/* Clip rectangles are logical pixels with a top-left origin, like 2D drawing,
 * while the scissor rect is in framebuffer pixels: screen rects scale by the DPI
 * scale, render targets are already in their own pixels. */
float sk_render_pixel_scale(void)
{
    return sk_render_current_pass_index == 0 ? sk_platform_dpi_scale() : 1.0f;
}

static void set_scissor(float x, float y, float width, float height)
{
    const float scale = sk_render_pixel_scale();
    sgl_scissor_rectf(x * scale, y * scale, width * scale, height * scale, true);
}

/* The clip stack. Each push intersects with the clip it's pushed inside, so nested
 * areas (a scroll list in a panel, a scene layer clip inside a UI clip) stay inside
 * their parent. Each render pass starts from its whole target: a render target's
 * pass (sk_render_begin_texture) ignores clips pushed on the screen before it. */
#define SK_MAX_CLIPS 32

typedef struct {
    float x, y, width, height;
} clip_rect_t;

static clip_rect_t sk_clips[SK_MAX_CLIPS];
static int sk_clip_depth;    /* entries in use */
static int sk_clip_base;     /* first entry that belongs to the current pass */
static int sk_clip_overflow; /* pushes past SK_MAX_CLIPS, so their pops match up */
static bool sk_clip_warned;

static void warn_clips(const char *what)
{
    if (!sk_clip_warned) {
        log_warn("render: %s", what);
        sk_clip_warned = true;
    }
}

/* The whole drawing target, in logical pixels (a render target's own pixels). */
static clip_rect_t target_rect(void)
{
    const vec2_t size = sk_render_current_pass_index == 0 ? sk_window_get_screen_size() : sk_render_target_size();
    return (clip_rect_t){0.0f, 0.0f, size.x, size.y};
}

static clip_rect_t current_clip(void)
{
    return sk_clip_depth > sk_clip_base ? sk_clips[sk_clip_depth - 1] : target_rect();
}

static void apply_clip(void)
{
    sk_render_revision++;
    const clip_rect_t clip = current_clip();
    set_scissor(clip.x, clip.y, clip.width, clip.height);
}

SK_KEEP
void sk_render_push_clip(float x, float y, float width, float height)
{
    const clip_rect_t parent = current_clip();
    const float x0 = x > parent.x ? x : parent.x;
    const float y0 = y > parent.y ? y : parent.y;
    const float x1 = x + (width > 0.0f ? width : 0.0f), px1 = parent.x + parent.width;
    const float y1 = y + (height > 0.0f ? height : 0.0f), py1 = parent.y + parent.height;
    const float right = x1 < px1 ? x1 : px1, bottom = y1 < py1 ? y1 : py1;

    if (sk_clip_depth >= SK_MAX_CLIPS) {
        sk_clip_overflow++;
        warn_clips("clip stack full (32 deep); extra pushes don't clip");
        return;
    }
    sk_clips[sk_clip_depth++] = (clip_rect_t){x0, y0, right > x0 ? right - x0 : 0.0f, bottom > y0 ? bottom - y0 : 0.0f};
    apply_clip();
}

SK_KEEP
void sk_render_pop_clip(void)
{
    if (sk_clip_overflow > 0) {
        sk_clip_overflow--;
        return;
    }
    if (sk_clip_depth <= sk_clip_base) {
        warn_clips("sk_render_pop_clip without a matching push");
        return;
    }
    sk_clip_depth--;
    apply_clip();
}

bool sk_render_get_clip(float *x, float *y, float *width, float *height)
{
    const clip_rect_t clip = current_clip();
    *x = clip.x;
    *y = clip.y;
    *width = clip.width;
    *height = clip.height;
    return sk_clip_depth > sk_clip_base;
}

/* A render target's pass starts with no clip; back on the screen, the screen's clips
 * apply again. Pushes left open in the target's pass are dropped. */
static void clip_begin_pass(void)
{
    sk_clip_base = sk_clip_depth;
}

static void clip_end_pass(void)
{
    if (sk_clip_depth > sk_clip_base || sk_clip_overflow > 0) {
        warn_clips("clips pushed while drawing into a texture weren't popped");
    }
    sk_clip_depth = sk_clip_base;
    sk_clip_overflow = 0;
    sk_clip_base = 0;
}

static void clip_end_frame(void)
{
    if (sk_clip_depth > 0 || sk_clip_overflow > 0) {
        warn_clips("clips pushed this frame weren't all popped");
    }
    sk_clip_depth = sk_clip_base = sk_clip_overflow = 0;
}

SK_KEEP
void sk_render_begin_mode_3d(void)
{
    sk_render_revision++;
    sk_camera3d_t cam;
    const vec2_t size = sk_render_target_size();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;

    if (!sk_camera3d_get_active_data(&cam)) {
        return;
    }

    sgl_defaults();
    sgl_load_pipeline(sk_pip_3d);
    sk_render_transparent_3d = false;

    /* same matrices as models and picking (sk_camera3d_projection / _view) */
    sgl_matrix_mode_projection();
    sgl_load_matrix(sk_camera3d_projection(&cam, aspect).m);
    sgl_matrix_mode_modelview();
    sgl_load_matrix(sk_camera3d_view(&cam).m);
}

SK_KEEP
void sk_render_end_mode_3d(void)
{
    setup_2d_projection();
}

unsigned sk_render_state_revision(void)
{
    return sk_render_revision;
}

int sk_render_command_count(void)
{
    return sk_render_cmd_count;
}
