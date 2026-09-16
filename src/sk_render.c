#include "sk_render.h"

#include <stdbool.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_model.h"
#include "internal/sk_render.h"
#include "sk_camera3d.h"
#include "sk_logger.h"

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_glue.h"
#include "util/sokol_gl.h"

#define SK_DEG2RAD 0.01745329251994329577f
#define MAX_RENDER_CMDS 1024

/* Render model
 * -----------
 * sokol_gl records draw commands into internal buffers during the frame and
 * replays them inside a live sg pass. raylib clears at BeginDrawing(); sokol
 * clears via the pass load-action. To keep the librl-style
 * begin/clear/draw/end ordering, we record everything between sk_render_begin()
 * and sk_render_end(), then open the swapchain pass in sk_render_end() (where
 * the clear color is already known) and replay the frame's command list (sgl
 * layers and model draws, in call order; see internal/sk_render.h), then the
 * debugtext overlay.
 */

typedef enum {
    RENDER_CMD_SGL_LAYER,
    RENDER_CMD_MODELS,
} sk_render_cmd_kind_t;

typedef struct {
    sk_render_cmd_kind_t kind;
    int layer; /* RENDER_CMD_SGL_LAYER */
    int first; /* RENDER_CMD_MODELS: item range in sk_model's queue */
    int count;
} sk_render_cmd_t;

static color_t sk_clear_color = {0.1f, 0.1f, 0.1f, 1.0f};
static sgl_pipeline sk_pip_2d;
static sgl_pipeline sk_pip_3d;
static sgl_pipeline sk_pip_3d_transparent;

static sk_render_cmd_t sk_render_cmds[MAX_RENDER_CMDS];
static int sk_render_cmd_count;
static int sk_render_next_layer;
/* sokol_gl totals when the current layer was opened, to detect empty layers */
static int sk_layer_mark_vertices;
static int sk_layer_mark_commands;
static bool sk_render_overflow_logged;

static void open_sgl_layer(void)
{
    int layer = sk_render_next_layer++;
    sgl_layer(layer);
    sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
        .kind = RENDER_CMD_SGL_LAYER,
        .layer = layer,
    };
    sk_layer_mark_vertices = sgl_num_vertices();
    sk_layer_mark_commands = sgl_num_commands();
}

static void reset_frame_commands(void)
{
    sk_render_cmd_count = 0;
    sk_render_next_layer = 0;
    open_sgl_layer();
}

void sk_render_submit_models(int first, int count)
{
    sk_render_cmd_t *last;

    if (count <= 0) {
        return;
    }

    /* drop the current sgl layer if nothing was recorded into it */
    last = &sk_render_cmds[sk_render_cmd_count - 1];
    if (sk_render_cmd_count > 1 && last->kind == RENDER_CMD_SGL_LAYER &&
        sgl_num_vertices() == sk_layer_mark_vertices &&
        sgl_num_commands() == sk_layer_mark_commands) {
        sk_render_cmd_count--;
        sk_render_next_layer--;
        last = &sk_render_cmds[sk_render_cmd_count - 1];
    }

    if (last->kind == RENDER_CMD_MODELS && last->first + last->count == first) {
        last->count += count; /* extend the adjacent model run */
    } else if (sk_render_cmd_count < MAX_RENDER_CMDS - 1) {
        sk_render_cmds[sk_render_cmd_count++] = (sk_render_cmd_t){
            .kind = RENDER_CMD_MODELS,
            .first = first,
            .count = count,
        };
    } else {
        /* out of commands: fold into the last model run (order may be off) */
        if (!sk_render_overflow_logged) {
            log_warn("render: MAX_RENDER_CMDS (%d) reached; draw order may be wrong", MAX_RENDER_CMDS);
            sk_render_overflow_logged = true;
        }
        for (int i = sk_render_cmd_count - 1; i >= 0; i--) {
            if (sk_render_cmds[i].kind == RENDER_CMD_MODELS) {
                sk_render_cmds[i].count = first + count - sk_render_cmds[i].first;
                return;
            }
        }
        return;
    }
    open_sgl_layer();
}

void sk_render_set_3d_transparent(bool transparent)
{
    sgl_load_pipeline(transparent ? sk_pip_3d_transparent : sk_pip_3d);
}

void sk_render_init(void)
{
    sgl_setup(&(sgl_desc_t){
        .logger.func = 0,
    });

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

    reset_frame_commands();
}

void sk_render_deinit(void)
{
    sgl_destroy_pipeline(sk_pip_2d);
    sgl_destroy_pipeline(sk_pip_3d);
    sgl_destroy_pipeline(sk_pip_3d_transparent);
    sgl_shutdown();
}

static void setup_2d_projection(void)
{
    const float w = (float)sapp_width();
    const float h = (float)sapp_height();

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
void sk_render_clear_background(sk_handle_t color)
{
    sk_clear_color = sk_color_get(color);
}

SK_KEEP
void sk_render_end(void)
{
    sg_pass pass = {
        .action = {
            .colors[0] = {
                .load_action = SG_LOADACTION_CLEAR,
                .clear_value = {sk_clear_color.r, sk_clear_color.g,
                                sk_clear_color.b, sk_clear_color.a},
            },
        },
        .swapchain = sglue_swapchain(),
    };

    sk_debug_draw();

    /* upload the font atlas before opening the pass (sg_update_image cannot run
     * inside a render pass) */
    sk_font_flush();

    sg_begin_pass(&pass);
    for (int i = 0; i < sk_render_cmd_count; i++) {
        const sk_render_cmd_t *cmd = &sk_render_cmds[i];
        if (cmd->kind == RENDER_CMD_SGL_LAYER) {
            sgl_draw_layer(cmd->layer); /* shapes / sprites / 2D / fontstash text */
        } else {
            sk_model_draw_items(cmd->first, cmd->count); /* custom-pipeline meshes */
        }
    }
    sk_text_flush(); /* debugtext overlay */
    sg_end_pass();
    sg_commit();

    sk_model_end_frame();
    reset_frame_commands();
}

SK_KEEP
void sk_render_begin_mode_2d(sk_handle_t camera)
{
    (void)camera;
    setup_2d_projection();
}

SK_KEEP
void sk_render_end_mode_2d(void)
{
    /* 2D is the default projection; nothing to restore for the milestone. */
}

SK_KEEP
void sk_render_begin_mode_3d(void)
{
    sk_camera3d_t cam;
    const float w = (float)sapp_width();
    const float h = (float)sapp_height();
    const float aspect = h > 0.0f ? w / h : 1.0f;

    if (!sk_camera3d_get_active_data(&cam)) {
        return;
    }

    sgl_defaults();
    sgl_load_pipeline(sk_pip_3d);

    sgl_matrix_mode_projection();
    sgl_load_identity();
    if (cam.projection == SK_CAMERA3D_ORTHOGRAPHIC) {
        const float top = cam.fovy * 0.5f;
        const float right = top * aspect;
        sgl_ortho(-right, right, -top, top, -1000.0f, 1000.0f);
    } else {
        sgl_perspective(cam.fovy * SK_DEG2RAD, aspect, 0.01f, 1000.0f);
    }

    sgl_matrix_mode_modelview();
    sgl_load_identity();
    sgl_lookat(cam.position.x, cam.position.y, cam.position.z,
               cam.target.x, cam.target.y, cam.target.z,
               cam.up.x, cam.up.y, cam.up.z);
}

SK_KEEP
void sk_render_end_mode_3d(void)
{
    setup_2d_projection();
}
