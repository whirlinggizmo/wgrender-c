#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_effect_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_shader_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr.h"
#include "wgr_logger.h"
#include "wgr_render.h"

/* Screen effects (post-processing, docs/PLAN-render-target.md): the frame draws into a
 * texture instead of the screen, and each effect's shader redraws it into the next
 * texture, the last one onto the screen. One triangle covering the screen per effect;
 * the shader is a screen shader (shaders/wgr.glsl, wgr_screen), which sees the frame as
 * wgr_screen_color().
 *
 * The textures are render targets like any other (wgr_texture_create_target), so they
 * match the screen's format and anti-aliasing and every pipeline that draws the frame
 * works in them unchanged. Two are enough: effect i reads one and writes the other. */

#define WGR_MAX_EFFECTS 8

static struct {
    wgr_handle_t materials[WGR_MAX_EFFECTS];
    int count;
    wgr_handle_t buffers[2]; /* render targets, made on first use and when the screen resizes */
    int width, height;
    bool warned; /* a shader that went missing: say so once */
} wgr_fx;

static void free_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        wgr_texture_release(wgr_fx.buffers[i]);
        wgr_fx.buffers[i] = 0;
    }
    wgr_fx.width = wgr_fx.height = 0;
}

/* The targets the chain needs at this size: one, or two when effects follow each
 * other. False when they can't be made. */
static bool ensure_buffers(int width, int height)
{
    const int wanted = wgr_fx.count > 1 ? 2 : 1;

    if (width != wgr_fx.width || height != wgr_fx.height) {
        free_buffers();
    }
    wgr_fx.width = width;
    wgr_fx.height = height;
    for (int i = 0; i < wanted; i++) {
        if (wgr_fx.buffers[i] == 0) {
            wgr_fx.buffers[i] = wgr_texture_create_target(width, height);
        }
        if (wgr_fx.buffers[i] == 0) {
            return false;
        }
    }
    return true;
}

/* The program a material's effect draws with, or NULL when it no longer has one (its
 * shader was released, or it isn't a screen shader after all). */
static const wgri_shader_program_t *effect_program(wgr_handle_t material, wgri_shader_t **shader_out)
{
    const wgri_material_t *material_ptr = wgri_material_get(material);
    wgri_shader_t *shader = material_ptr != NULL && material_ptr->shader != 0 && wgri_shader_hooks.get != NULL
                              ? wgri_shader_hooks.get(material_ptr->shader)
                              : NULL;
    if (shader == NULL || !shader->screen) {
        return NULL;
    }
    *shader_out = shader;
    return &shader->programs[WGRI_SHADER_PROGRAM_SCREEN];
}

/* Draw `material` over `source` (the frame so far) into the open pass. */
static void draw_effect(wgr_handle_t material, wgr_handle_t source)
{
    const wgri_material_t *material_ptr = wgri_material_get(material);
    wgri_shader_t *shader = NULL;
    const wgri_shader_program_t *program = effect_program(material, &shader);
    sg_bindings bind = {0};
    sg_view view = {0};
    float info[4];

    if (program == NULL || material_ptr == NULL) {
        if (!wgr_fx.warned) {
            log_warn("render: a screen effect's material lost its shader; the frame is drawn as it is");
            wgr_fx.warned = true;
        }
        return;
    }
    if (shader->screen_pipeline.id == SG_INVALID_ID) {
        /* the frame covers every pixel: no depth test, no blending, no culling */
        shader->screen_pipeline = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = program->shader,
            .cull_mode = SG_CULLMODE_NONE,
            .depth = {.compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false},
            .label = "wgr-screen-effect",
        });
    }
    sg_apply_pipeline(shader->screen_pipeline);

    info[0] = (float)wgr_fx.width;
    info[1] = (float)wgr_fx.height;
    info[2] = (float)wgr_get_time();
    info[3] = wgri_texture_is_flipped(source) ? 1.0f : 0.0f;
    if (program->has_block[WGRI_SHADER_BLOCK_FRAME]) {
        sg_apply_uniforms(WGRI_SHADER_BLOCK_FRAME, &SG_RANGE(info));
    }
    if (program->has_block[WGRI_SHADER_BLOCK_FS_PARAMS]) {
        sg_apply_uniforms(WGRI_SHADER_BLOCK_FS_PARAMS,
                          &(sg_range){.ptr = material_ptr->custom_params,
                                      .size = (size_t)shader->block_size[WGRI_SHADER_BLOCK_FS_PARAMS]});
    }
    if (program->screen_view_slot >= 0 && wgri_texture_get_binding(source, &view, NULL, NULL, NULL)) {
        bind.views[program->screen_view_slot] = view;
    }
    if (program->screen_sampler_slot >= 0) {
        bind.samplers[program->screen_sampler_slot] =
            wgri_texture_sampler(WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_FILTER_LINEAR, false);
    }
    for (int t = 0; t < shader->texture_count; t++) { /* the effect's own textures (a noise map, a LUT) */
        const wgri_material_texture_t *texture = &material_ptr->textures[t];
        sg_view texture_view = {0};
        sg_view white, black_cube;
        sg_sampler linear;
        wgri_shader_hooks.fallbacks(&white, &black_cube, &linear);
        texture_view = white;
        if (texture->texture != 0) wgri_texture_get_binding(texture->texture, &texture_view, NULL, NULL, NULL);
        if (program->view_slot[t] >= 0) bind.views[program->view_slot[t]] = texture_view;
        if (program->sampler_slot[t] >= 0) {
            bind.samplers[program->sampler_slot[t]] =
                wgri_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        }
    }
    sg_apply_bindings(&bind);
    sg_draw(0, 3, 1); /* one triangle, built in the vertex shader */
}

/* wgri_render_hooks: where the screen's pass draws. */
static bool effects_begin(sg_attachments *attachments)
{
    const vec2_t size = wgri_render_target_size();
    const int width = (int)(size.x + 0.5f), height = (int)(size.y + 0.5f);

    if (wgr_fx.count == 0 || width <= 0 || height <= 0 || !ensure_buffers(width, height)) {
        return false;
    }
    return wgri_texture_get_target(wgr_fx.buffers[0], attachments, NULL, NULL);
}

/* wgri_render_hooks: the chain, ending on the screen. */
static void effects_draw(void)
{
    for (int i = 0; i < wgr_fx.count; i++) {
        const bool last = i == wgr_fx.count - 1;
        const wgr_handle_t source = wgr_fx.buffers[i % 2];
        sg_attachments attachments;
        sg_pass pass = {.action = {.colors[0].load_action = SG_LOADACTION_DONTCARE, /* every pixel is written */
                                   .depth.load_action = SG_LOADACTION_DONTCARE,
                                   .stencil.load_action = SG_LOADACTION_DONTCARE}};
        if (last) {
            pass.swapchain = wgri_platform_swapchain();
        } else if (wgri_texture_get_target(wgr_fx.buffers[(i + 1) % 2], &attachments, NULL, NULL)) {
            pass.attachments = attachments;
        } else {
            return;
        }
        pass.label = last ? "wgr-screen-effect-final" : "wgr-screen-effect";
        sg_begin_pass(&pass);
        draw_effect(wgr_fx.materials[i], source);
        sg_end_pass();
    }
}

/* ---------------------------------------------------------- public API ---- */

WGRI_KEEP
bool wgr_render_add_effect(wgr_handle_t material)
{
    if (wgri_material_get(material) == NULL) {
        log_warn("wgr_render_add_effect: needs a material made with wgr_material_create_custom");
        return false;
    }
    if (!wgri_material_is_screen(material)) {
        log_warn("wgr_render_add_effect: that material's shader draws surfaces; a screen effect's fragment shader "
                 "includes wgr_screen (see shaders/wgr.glsl)");
        return false;
    }
    if (wgr_fx.count >= WGR_MAX_EFFECTS) {
        log_warn("wgr_render_add_effect: at most %d effects", WGR_MAX_EFFECTS);
        return false;
    }
    wgri_material_retain(material);
    wgr_fx.materials[wgr_fx.count++] = material;
    return true;
}

WGRI_KEEP
void wgr_render_clear_effects(void)
{
    for (int i = 0; i < wgr_fx.count; i++) {
        wgr_material_release(wgr_fx.materials[i]);
        wgr_fx.materials[i] = 0;
    }
    wgr_fx.count = 0;
    free_buffers(); /* nothing to draw into until an effect comes back */
}

WGRI_KEEP
int wgr_render_effect_count(void)
{
    return wgr_fx.count;
}

void wgri_effect_init(void)
{
    wgri_render_hooks.effects_begin = effects_begin;
    wgri_render_hooks.effects_draw = effects_draw;
}

void wgri_effect_deinit(void)
{
    wgr_render_clear_effects();
    wgri_render_hooks.effects_begin = NULL;
    wgri_render_hooks.effects_draw = NULL;
    memset(&wgr_fx, 0, sizeof(wgr_fx));
}

/* An optional subsystem: part of the runtime when a program adds an effect
 * (internal/wgri_module.h). After materials, whose handles it holds. */
static wgri_module_t wgr_effect_module = {.name = "effect", .order = 35, .init = wgri_effect_init,
                                       .deinit = wgri_effect_deinit};
WGRI_MODULE(wgr_effect_module)
