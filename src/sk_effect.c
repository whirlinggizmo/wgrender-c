#include <string.h>

#include "internal/exports.h"
#include "internal/sk_material.h"
#include "internal/sk_effect.h"
#include "internal/sk_module.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_shader.h"
#include "internal/sk_texture.h"
#include "sk.h"
#include "sk_logger.h"
#include "sk_render.h"

/* Screen effects (post-processing, docs/PLAN-render-target.md): the frame draws into a
 * texture instead of the screen, and each effect's shader redraws it into the next
 * texture, the last one onto the screen. One triangle covering the screen per effect;
 * the shader is a screen shader (shaders/sk.glsl, sk_screen), which sees the frame as
 * sk_screen_color().
 *
 * The textures are render targets like any other (sk_texture_create_target), so they
 * match the screen's format and anti-aliasing and every pipeline that draws the frame
 * works in them unchanged. Two are enough: effect i reads one and writes the other. */

#define SK_MAX_EFFECTS 8

static struct {
    sk_handle_t materials[SK_MAX_EFFECTS];
    int count;
    sk_handle_t buffers[2]; /* render targets, made on first use and when the screen resizes */
    int width, height;
    bool warned; /* a shader that went missing: say so once */
} sk_fx;

static void free_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        sk_texture_release(sk_fx.buffers[i]);
        sk_fx.buffers[i] = 0;
    }
    sk_fx.width = sk_fx.height = 0;
}

/* The targets the chain needs at this size: one, or two when effects follow each
 * other. False when they can't be made. */
static bool ensure_buffers(int width, int height)
{
    const int wanted = sk_fx.count > 1 ? 2 : 1;

    if (width != sk_fx.width || height != sk_fx.height) {
        free_buffers();
    }
    sk_fx.width = width;
    sk_fx.height = height;
    for (int i = 0; i < wanted; i++) {
        if (sk_fx.buffers[i] == 0) {
            sk_fx.buffers[i] = sk_texture_create_target(width, height);
        }
        if (sk_fx.buffers[i] == 0) {
            return false;
        }
    }
    return true;
}

/* The program a material's effect draws with, or NULL when it no longer has one (its
 * shader was released, or it isn't a screen shader after all). */
static const sk_shader_program_t *effect_program(sk_handle_t material, sk_shader_t **shader_out)
{
    const sk_material_t *material_ptr = sk_material_get(material);
    sk_shader_t *shader = material_ptr != NULL && material_ptr->shader != 0 && sk_shader_hooks.get != NULL
                              ? sk_shader_hooks.get(material_ptr->shader)
                              : NULL;
    if (shader == NULL || !shader->screen) {
        return NULL;
    }
    *shader_out = shader;
    return &shader->programs[SK_SHADER_PROGRAM_SCREEN];
}

/* Draw `material` over `source` (the frame so far) into the open pass. */
static void draw_effect(sk_handle_t material, sk_handle_t source)
{
    const sk_material_t *material_ptr = sk_material_get(material);
    sk_shader_t *shader = NULL;
    const sk_shader_program_t *program = effect_program(material, &shader);
    sg_bindings bind = {0};
    sg_view view = {0};
    float info[4];

    if (program == NULL || material_ptr == NULL) {
        if (!sk_fx.warned) {
            log_warn("render: a screen effect's material lost its shader; the frame is drawn as it is");
            sk_fx.warned = true;
        }
        return;
    }
    if (shader->screen_pipeline.id == SG_INVALID_ID) {
        /* the frame covers every pixel: no depth test, no blending, no culling */
        shader->screen_pipeline = sg_make_pipeline(&(sg_pipeline_desc){
            .shader = program->shader,
            .cull_mode = SG_CULLMODE_NONE,
            .depth = {.compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false},
            .label = "sk-screen-effect",
        });
    }
    sg_apply_pipeline(shader->screen_pipeline);

    info[0] = (float)sk_fx.width;
    info[1] = (float)sk_fx.height;
    info[2] = (float)sk_get_time();
    info[3] = sk_texture_is_flipped(source) ? 1.0f : 0.0f;
    if (program->has_block[SK_SHADER_BLOCK_FRAME]) {
        sg_apply_uniforms(SK_SHADER_BLOCK_FRAME, &SG_RANGE(info));
    }
    if (program->has_block[SK_SHADER_BLOCK_FS_PARAMS]) {
        sg_apply_uniforms(SK_SHADER_BLOCK_FS_PARAMS,
                          &(sg_range){.ptr = material_ptr->custom_params,
                                      .size = (size_t)shader->block_size[SK_SHADER_BLOCK_FS_PARAMS]});
    }
    if (program->screen_view_slot >= 0 && sk_texture_get_binding(source, &view, NULL, NULL, NULL)) {
        bind.views[program->screen_view_slot] = view;
    }
    if (program->screen_sampler_slot >= 0) {
        bind.samplers[program->screen_sampler_slot] =
            sk_texture_sampler(SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_LINEAR, false);
    }
    for (int t = 0; t < shader->texture_count; t++) { /* the effect's own textures (a noise map, a LUT) */
        const sk_material_texture_t *texture = &material_ptr->textures[t];
        sg_view texture_view = {0};
        sg_view white, black_cube;
        sg_sampler linear;
        sk_shader_hooks.fallbacks(&white, &black_cube, &linear);
        texture_view = white;
        if (texture->texture != 0) sk_texture_get_binding(texture->texture, &texture_view, NULL, NULL, NULL);
        if (program->view_slot[t] >= 0) bind.views[program->view_slot[t]] = texture_view;
        if (program->sampler_slot[t] >= 0) {
            bind.samplers[program->sampler_slot[t]] =
                sk_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        }
    }
    sg_apply_bindings(&bind);
    sg_draw(0, 3, 1); /* one triangle, built in the vertex shader */
}

/* sk_render_hooks: where the screen's pass draws. */
static bool effects_begin(sg_attachments *attachments)
{
    const vec2_t size = sk_render_target_size();
    const int width = (int)(size.x + 0.5f), height = (int)(size.y + 0.5f);

    if (sk_fx.count == 0 || width <= 0 || height <= 0 || !ensure_buffers(width, height)) {
        return false;
    }
    return sk_texture_get_target(sk_fx.buffers[0], attachments, NULL, NULL);
}

/* sk_render_hooks: the chain, ending on the screen. */
static void effects_draw(void)
{
    for (int i = 0; i < sk_fx.count; i++) {
        const bool last = i == sk_fx.count - 1;
        const sk_handle_t source = sk_fx.buffers[i % 2];
        sg_attachments attachments;
        sg_pass pass = {.action = {.colors[0].load_action = SG_LOADACTION_DONTCARE, /* every pixel is written */
                                   .depth.load_action = SG_LOADACTION_DONTCARE,
                                   .stencil.load_action = SG_LOADACTION_DONTCARE}};
        if (last) {
            pass.swapchain = sk_platform_swapchain();
        } else if (sk_texture_get_target(sk_fx.buffers[(i + 1) % 2], &attachments, NULL, NULL)) {
            pass.attachments = attachments;
        } else {
            return;
        }
        pass.label = last ? "sk-screen-effect-final" : "sk-screen-effect";
        sg_begin_pass(&pass);
        draw_effect(sk_fx.materials[i], source);
        sg_end_pass();
    }
}

/* ---------------------------------------------------------- public API ---- */

SK_KEEP
bool sk_render_add_effect(sk_handle_t material)
{
    if (sk_material_get(material) == NULL) {
        log_warn("sk_render_add_effect: needs a material made with sk_material_create_custom");
        return false;
    }
    if (!sk_material_is_screen(material)) {
        log_warn("sk_render_add_effect: that material's shader draws surfaces; a screen effect's fragment shader "
                 "includes sk_screen (see shaders/sk.glsl)");
        return false;
    }
    if (sk_fx.count >= SK_MAX_EFFECTS) {
        log_warn("sk_render_add_effect: at most %d effects", SK_MAX_EFFECTS);
        return false;
    }
    sk_material_retain(material);
    sk_fx.materials[sk_fx.count++] = material;
    return true;
}

SK_KEEP
void sk_render_clear_effects(void)
{
    for (int i = 0; i < sk_fx.count; i++) {
        sk_material_release(sk_fx.materials[i]);
        sk_fx.materials[i] = 0;
    }
    sk_fx.count = 0;
    free_buffers(); /* nothing to draw into until an effect comes back */
}

SK_KEEP
int sk_render_effect_count(void)
{
    return sk_fx.count;
}

void sk_effect_init(void)
{
    sk_render_hooks.effects_begin = effects_begin;
    sk_render_hooks.effects_draw = effects_draw;
}

void sk_effect_deinit(void)
{
    sk_render_clear_effects();
    sk_render_hooks.effects_begin = NULL;
    sk_render_hooks.effects_draw = NULL;
    memset(&sk_fx, 0, sizeof(sk_fx));
}

/* An optional subsystem: part of the runtime when a program adds an effect
 * (internal/sk_module.h). After materials, whose handles it holds. */
static sk_module_t sk_effect_module = {.name = "effect", .order = 35, .init = sk_effect_init,
                                       .deinit = sk_effect_deinit};
SK_MODULE(sk_effect_module)
