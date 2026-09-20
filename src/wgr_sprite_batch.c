#include "internal/wgr_sprite_batch_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_environment_internal.h"
#include "internal/wgr_light_internal.h"
#include "internal/wgr_material_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_shader_internal.h"
#include "internal/wgr_shadow_internal.h"
#include "internal/wgr_shaders_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_texture_internal.h"
#include "wgr.h" /* wgr_get_time */
#include "wgr_logger.h"
#include "wgr_window.h"

/* Instanced sprites (docs/PLAN-sprites.md). During the frame, sprites are appended to
 * one instance array in call order and grouped into batches: consecutive sprites
 * with the same texture, camera, clip, depth mode and pass. Each batch is a render
 * command. Before the passes, the instances go up in one buffer update; each batch
 * then draws its range with one instanced draw. */

#define INSTANCES_INITIAL 1024
/* Without base-instance draws (WebGL2, GL before 4.2) the sprites go into a float
 * texture the shader reads by index (quad_pulled in wgr_sprite.glsl): 6 texels a
 * sprite, 256 sprites a row. */
#define PULLED_TEXELS 6
#define PULLED_SPRITES_PER_ROW 256
#define PULLED_WIDTH (PULLED_TEXELS * PULLED_SPRITES_PER_ROW)
#define BATCHES_INITIAL 64
#define CAMERAS_INITIAL 8

enum {
    PIPELINE_OPAQUE,             /* opaque and masked: no blending, depth written */
    PIPELINE_BLEND_DEPTH_WRITE,  /* blended, depth written (direct draws) */
    PIPELINE_BLEND,              /* blended, depth not written (a scene's sorted pass) */
    PIPELINE_ADD,                /* added, depth not written */
    PIPELINE_2D_OPAQUE,          /* 2D: no depth test; opaque and masked */
    PIPELINE_2D_BLEND,
    PIPELINE_2D_ADD,
    PIPELINE_COUNT
};

/* What sprites drawn now are drawn with: camera uniforms, pass and scissor. Kept until
 * the render state or a camera changes. */
typedef struct {
    bool valid;
    unsigned render_revision, camera_revision;
    int camera, pass;
    float scissor[4];
} wgr_sprite_state_t;

/* A sprite held back while unordered, to be grouped with others like it. */
typedef struct {
    wgr_sprite_quad_t quad;
    uint32_t view, sampler;
    int pipeline;
    wgr_handle_t material;
} wgr_sprite_pending_t;

typedef struct {
    uint32_t view, sampler;
    int pipeline;
    wgr_handle_t material; /* the material drawing it (custom or built-in), or 0 */
    bool lit;             /* a built-in material: libwgrender's model shading, with lights */
    int light_env;        /* the lighting environment it was recorded in, -1 = none */
    vec3_t bounds_min, bounds_max; /* its sprites, for choosing lights once a batch */
    int camera; /* index into the frame's camera uniforms */
    int pass;
    float scissor[4]; /* framebuffer pixels: x, y, width, height (the clip, or the whole target) */
    int first, count;
} wgr_sprite_batch_t;

/* A camera as the shader needs it, and what it was made from (to reuse it). */
typedef struct {
    wgr_camera3d_t source;
    float aspect;
    sprite_vs_params_t params;
} wgr_sprite_camera_t;

static struct {
    bool ready;
    bool base_instance; /* the backend can draw from a base instance (not WebGL2) */
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    /* built-in materials: libwgrender's model shading (src/shaders/wgr_pbr.glsl), made on first use */
    sg_shader lit_shader;
    sg_pipeline lit_pipelines[PIPELINE_COUNT];
    sg_image white, flat_normal, black_cube; /* stand-ins for the material's maps and the environment */
    sg_view white_view, flat_normal_view, black_cube_view;
    sg_sampler lit_sampler;
    /* where a shadow map goes when nothing casts (never read: the shadow factor is 1) */
    sg_image no_shadow;
    sg_view no_shadow_view;
    sg_sampler no_shadow_sampler;
    /* no base instance: the sprites as texels, read by index */
    sg_image data_image;
    sg_view data_view;
    sg_sampler data_sampler;
    int data_rows;         /* the image's height */
    float *data;           /* texels for the frame's sprites, PULLED_WIDTH floats x 4 a row */
    int data_capacity;     /* sprites `data` holds */
    sg_buffer quad;
    sg_buffer instance_buffer;
    int instance_buffer_capacity;
    wgr_sprite_quad_t *instances;
    int instance_count, instance_capacity;
    wgr_sprite_batch_t *batches;
    int batch_count, batch_capacity;
    wgr_sprite_camera_t *cameras;
    int camera_count, camera_capacity;
    bool unordered;
    wgr_sprite_pending_t *pending;
    int pending_count, pending_capacity;
    int *order; /* pending sprites grouped by texture and mode (order_capacity long) */
    int order_capacity;
    bool overflow_logged;
    int last_drawn; /* the batch drawn last (replay), whose state may still be applied */
    /* the camera, pass and scissor sprites are drawn with now, until the render state
       or a camera changes */
    wgr_sprite_state_t state_3d, state_2d;
} wgr_sb;

static sg_backend shader_backend(void)
{
    const sg_backend backend = sg_query_backend();
    return backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend;
}

/* Grow *items (count used, capacity) to hold one more, doubling. */
static bool reserve(void **items, int *capacity, int count, size_t item_size, int initial)
{
    if (count < *capacity) {
        return true;
    }
    const int grown = *capacity > 0 ? *capacity * 2 : initial;
    void *moved = realloc(*items, item_size * (size_t)grown);
    if (moved == NULL) {
        if (!wgr_sb.overflow_logged) {
            log_error("sprites: out of memory");
            wgr_sb.overflow_logged = true;
        }
        return false;
    }
    *items = moved;
    *capacity = grown;
    return true;
}

/* Started by sprite3d and sprite2d, whichever come first, and stopped with the last. */
static int wgr_sb_users;

/* A lit sprite in this lighting environment: it receives shadows (sprites don't cast).
 * wgr_render_hooks.sprites_lit_in. */
static bool wgr_sprite_batch_lit_in(int light_env)
{
    for (int i = 0; i < wgr_sb.batch_count; i++) {
        if (wgr_sb.batches[i].lit && wgr_sb.batches[i].light_env == light_env && wgr_sb.batches[i].count > 0) {
            return true;
        }
    }
    return false;
}

void wgr_sprite_batch_init(void)
{
    if (wgr_sb_users++ > 0) return;
    wgr_render_hooks.draw_sprites = wgr_sprite_batch_draw;
    wgr_render_hooks.sprites_lit_in = wgr_sprite_batch_lit_in;
    wgr_scene_hooks.sprites_begin_unordered = wgr_sprite_batch_begin_unordered;
    wgr_scene_hooks.sprites_end_unordered = wgr_sprite_batch_end_unordered;
    memset(&wgr_sb, 0, sizeof(wgr_sb));
    wgr_sb.base_instance = sg_query_features().draw_base_instance;
#ifdef WGR_SPRITES_PULLED /* build-time switch: read sprites from the texture everywhere (tests, benchmarks) */
    wgr_sb.base_instance = false;
#endif
    wgr_sb.last_drawn = -1;
}

/* The pipeline kinds (PIPELINE_*) for `shader`: libwgrender's sprite shader, or a custom
 * shader's sprite program (the same vertex layout: shaders/wgr.glsl). */
static void make_pipelines(sg_shader shader, sg_pipeline out[PIPELINE_COUNT])
{
    sg_pipeline_desc desc = {
        .layout = {
            .buffers[1] = {.step_func = SG_VERTEXSTEP_PER_INSTANCE},
            .attrs = {
                [ATTR_sprite_quad_corner] = {.format = SG_VERTEXFORMAT_FLOAT2, .buffer_index = 0},
                [ATTR_sprite_quad_inst_pos] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_size] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_uv] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_right] = {.format = SG_VERTEXFORMAT_FLOAT3, .buffer_index = 1},
                [ATTR_sprite_quad_inst_up] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_color] = {.format = SG_VERTEXFORMAT_UBYTE4N, .buffer_index = 1},
            },
        },
        /* like sokol_gl's 3D pipelines (wgr_render.c): blended, depth-tested, no culling */
        .depth = {.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
        .label = "wgr-sprite",
    };

    if (!wgr_sb.base_instance) { /* the sprites come from a texture: only the quad's corners are attributes */
        desc.layout = (sg_vertex_layout_state){
            .attrs[ATTR_sprite_quad_pulled_corner] = {.format = SG_VERTEXFORMAT_FLOAT2},
        };
    }
    desc.shader = shader;
    out[PIPELINE_BLEND_DEPTH_WRITE] = sg_make_pipeline(&desc);
    desc.depth.write_enabled = false;
    out[PIPELINE_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE; /* added: src x alpha + dst */
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
    out[PIPELINE_ADD] = sg_make_pipeline(&desc);
    desc.colors[0].blend = (sg_blend_state){0};
    desc.depth.write_enabled = true;
    out[PIPELINE_OPAQUE] = sg_make_pipeline(&desc);
    /* 2D, like sokol_gl's 2D pipeline: no depth test or write */
    desc.depth = (sg_depth_state){.compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false};
    out[PIPELINE_2D_OPAQUE] = sg_make_pipeline(&desc);
    desc.colors[0].blend = (sg_blend_state){
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    out[PIPELINE_2D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
    out[PIPELINE_2D_ADD] = sg_make_pipeline(&desc);
}

/* The shader, pipelines and quad, made with the first sprite drawn rather than at
 * startup: a program without sprites doesn't compile them (tools/webstart.mjs). */
static void ensure_gpu(void)
{
    /* two triangles: the quad's corners, x right, y up */
    static const float corners[12] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};

    if (wgr_sb.ready) return;
    if (wgr_sb.base_instance) {
        wgr_sb.shader = sg_make_shader(sprite_quad_shader_desc(shader_backend()));
    } else {
        wgr_sb.shader = sg_make_shader(sprite_quad_pulled_shader_desc(shader_backend()));
        wgr_sb.data_sampler = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = SG_FILTER_NEAREST,
            .mag_filter = SG_FILTER_NEAREST,
            .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
            .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
            .label = "wgr-sprite-data",
        });
    }
    make_pipelines(wgr_sb.shader, wgr_sb.pipelines);
    wgr_sb.quad = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(corners), .label = "wgr-sprite-quad"});
    wgr_sb.ready = true;
}

void wgr_sprite_batch_deinit(void)
{
    if (wgr_sb_users == 0 || --wgr_sb_users > 0) return;
    wgr_render_hooks.draw_sprites = NULL;
    wgr_render_hooks.sprites_lit_in = NULL;
    wgr_scene_hooks.sprites_begin_unordered = NULL;
    wgr_scene_hooks.sprites_end_unordered = NULL;
    if (wgr_sb.ready) {
        sg_destroy_buffer(wgr_sb.instance_buffer);
        sg_destroy_view(wgr_sb.data_view);
        sg_destroy_image(wgr_sb.data_image);
        sg_destroy_sampler(wgr_sb.data_sampler);
        sg_destroy_buffer(wgr_sb.quad);
        for (int i = 0; i < PIPELINE_COUNT; i++) {
            sg_destroy_pipeline(wgr_sb.pipelines[i]);
            sg_destroy_pipeline(wgr_sb.lit_pipelines[i]);
        }
        sg_destroy_shader(wgr_sb.shader);
        sg_destroy_shader(wgr_sb.lit_shader);
        sg_destroy_view(wgr_sb.white_view);
        sg_destroy_view(wgr_sb.flat_normal_view);
        sg_destroy_view(wgr_sb.black_cube_view);
        sg_destroy_image(wgr_sb.white);
        sg_destroy_image(wgr_sb.flat_normal);
        sg_destroy_image(wgr_sb.black_cube);
        sg_destroy_sampler(wgr_sb.lit_sampler);
        sg_destroy_sampler(wgr_sb.no_shadow_sampler);
        sg_destroy_view(wgr_sb.no_shadow_view);
        sg_destroy_image(wgr_sb.no_shadow);
    }
    free(wgr_sb.data);
    free(wgr_sb.instances);
    free(wgr_sb.batches);
    free(wgr_sb.cameras);
    free(wgr_sb.pending);
    free(wgr_sb.order);
    memset(&wgr_sb, 0, sizeof(wgr_sb));
}

/* The frame's uniforms for the active camera in the current pass, made once and
 * reused while the camera and the target's aspect stay the same; -1 if there's none. */
static int current_camera(void)
{
    wgr_camera3d_t cam;
    const vec2_t size = wgr_render_target_size();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
    wgr_sprite_camera_t *c;
    vec3_t right, up, upright, unused;

    if (!wgr_camera3d_get_active_data(&cam)) {
        return -1;
    }
    if (wgr_sb.camera_count > 0) {
        c = &wgr_sb.cameras[wgr_sb.camera_count - 1];
        if (c->aspect == aspect && memcmp(&c->source, &cam, sizeof(cam)) == 0) {
            return wgr_sb.camera_count - 1;
        }
    }
    if (!reserve((void **)&wgr_sb.cameras, &wgr_sb.camera_capacity, wgr_sb.camera_count, sizeof(wgr_sprite_camera_t),
                 CAMERAS_INITIAL)) {
        return -1;
    }
    c = &wgr_sb.cameras[wgr_sb.camera_count];
    c->source = cam;
    c->aspect = aspect;
    const wgr_mat4_t view_proj = wgr_mat4_mul(wgr_camera3d_projection(&cam, aspect), wgr_camera3d_view(&cam));
    memcpy(c->params.view_proj, view_proj.m, sizeof(c->params.view_proj));
    /* the billboard axes, exactly as picking works them out */
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
    wgr_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA_FIXED_Y, (vec3_t){0, 0, 0}, &cam, &upright, &unused);
    memcpy(c->params.camera_right, (float[4]){right.x, right.y, right.z, 0.0f}, sizeof(c->params.camera_right));
    memcpy(c->params.camera_up, (float[4]){up.x, up.y, up.z, 0.0f}, sizeof(c->params.camera_up));
    memcpy(c->params.upright, (float[4]){upright.x, upright.y, upright.z, 0.0f}, sizeof(c->params.upright));
    return wgr_sb.camera_count++;
}

/* 2D: the current target in logical pixels (render targets in their own pixels), top-left
 * origin, y down, as sokol_gl's 2D projection (setup_2d_projection). */
static int current_camera_2d(void)
{
    const vec2_t size = wgr_render_current_pass() == 0 ? wgr_window_get_screen_size() : wgr_render_target_size();
    const wgr_mat4_t ortho = wgr_mat4_ortho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f);
    wgr_sprite_camera_t *c;

    if (!reserve((void **)&wgr_sb.cameras, &wgr_sb.camera_capacity, wgr_sb.camera_count, sizeof(wgr_sprite_camera_t),
                 CAMERAS_INITIAL)) {
        return -1;
    }
    c = &wgr_sb.cameras[wgr_sb.camera_count];
    *c = (wgr_sprite_camera_t){.aspect = -1.0f}; /* never matches a 3D camera */
    memcpy(c->params.view_proj, ortho.m, sizeof(c->params.view_proj));
    return wgr_sb.camera_count++;
}

/* The camera uniforms, pass and scissor for sprites drawn now (3D: the active camera;
 * 2D: the screen's pixels); NULL without a camera. */
static const wgr_sprite_state_t *current_state(bool two_d)
{
    wgr_sprite_state_t *st = two_d ? &wgr_sb.state_2d : &wgr_sb.state_3d;
    const unsigned render_revision = wgr_render_state_revision();
    const unsigned camera_revision = two_d ? 0u : wgr_camera3d_revision();
    float x, y, w, h, scale;

    if (st->valid && st->render_revision == render_revision && st->camera_revision == camera_revision) {
        return st;
    }
    st->camera = two_d ? current_camera_2d() : current_camera();
    if (st->camera < 0) {
        st->valid = false;
        return NULL;
    }
    st->pass = wgr_render_current_pass();
    wgr_render_get_clip(&x, &y, &w, &h); /* the whole target when nothing is pushed */
    scale = wgr_render_pixel_scale();
    st->scissor[0] = x * scale, st->scissor[1] = y * scale;
    st->scissor[2] = w * scale, st->scissor[3] = h * scale;
    st->render_revision = render_revision;
    st->camera_revision = camera_revision;
    st->valid = true;
    return st;
}

/* Add one sprite to the frame, joining the open batch when it can. */
/* A built-in material (not a custom shader's): drawn with libwgrender's model shading. */
static bool material_is_lit(wgr_handle_t material)
{
    const wgr_material_t *material_ptr = material != 0 ? wgr_material_get(material) : NULL;
    return material_ptr != NULL && material_ptr->shader == 0;
}

static void record(const wgr_sprite_quad_t *instance, uint32_t view, uint32_t sampler, int pipeline,
                   wgr_handle_t material)
{
    const bool lit = material_is_lit(material);
    /* any material's sprites are lit: a custom shader gets the same lights (wgr_frame) */
    const int light_env = material != 0 ? wgr_light_env_current() : -1;
    const wgr_sprite_state_t *st;
    wgr_sprite_batch_t *last;

    ensure_gpu();
    st = wgr_sb.ready ? current_state(pipeline >= PIPELINE_2D_OPAQUE) : NULL;
    if (st == NULL) {
        return;
    }
    if (!reserve((void **)&wgr_sb.instances, &wgr_sb.instance_capacity, wgr_sb.instance_count, sizeof(wgr_sprite_quad_t),
                 INSTANCES_INITIAL)) {
        return;
    }
    last = wgr_sb.batch_count > 0 ? &wgr_sb.batches[wgr_sb.batch_count - 1] : NULL;
    /* extend the open batch when nothing else was drawn since and everything matches */
    if (last == NULL || last->view != view || last->sampler != sampler || last->pipeline != pipeline ||
        last->material != material || last->light_env != light_env ||
        last->camera != st->camera || last->pass != st->pass ||
        memcmp(last->scissor, st->scissor, sizeof(last->scissor)) != 0 ||
        !wgr_render_sprites_open(wgr_sb.batch_count - 1)) {
        if (!reserve((void **)&wgr_sb.batches, &wgr_sb.batch_capacity, wgr_sb.batch_count, sizeof(wgr_sprite_batch_t),
                     BATCHES_INITIAL)) {
            return;
        }
        if (!wgr_render_submit_sprites(wgr_sb.batch_count)) {
            return; /* out of render commands (logged there) */
        }
        last = &wgr_sb.batches[wgr_sb.batch_count++];
        *last = (wgr_sprite_batch_t){
            .view = view,
            .sampler = sampler,
            .pipeline = pipeline,
            .material = material,
            .lit = lit,
            .light_env = light_env,
            .bounds_min = {1e30f, 1e30f, 1e30f},
            .bounds_max = {-1e30f, -1e30f, -1e30f},
            .camera = st->camera,
            .pass = st->pass,
            .first = wgr_sb.instance_count,
        };
        memcpy(last->scissor, st->scissor, sizeof(last->scissor));
    }
    if (material != 0) { /* the batch's lights are chosen once, from where its sprites are */
        const float *p = instance->position;
        last->bounds_min = (vec3_t){fminf(last->bounds_min.x, p[0]), fminf(last->bounds_min.y, p[1]),
                                    fminf(last->bounds_min.z, p[2])};
        last->bounds_max = (vec3_t){fmaxf(last->bounds_max.x, p[0]), fmaxf(last->bounds_max.y, p[1]),
                                    fmaxf(last->bounds_max.z, p[2])};
    }
    wgr_sb.instances[wgr_sb.instance_count++] = *instance;
    last->count++;
}

void wgr_sprite_batch_add_3d(const wgr_sprite_quad_t *instance, uint32_t view, uint32_t sampler, wgr_alpha_mode_t mode,
                            bool blend_depth_write, wgr_handle_t material)
{
    const int pipeline = mode == WGR_ALPHA_OPAQUE || mode == WGR_ALPHA_MASK ? PIPELINE_OPAQUE
                         : mode == WGR_ALPHA_ADD                           ? PIPELINE_ADD
                         : blend_depth_write                              ? PIPELINE_BLEND_DEPTH_WRITE
                                                                          : PIPELINE_BLEND;
    if (!wgr_sb.unordered) {
        record(instance, view, sampler, pipeline, material);
        return;
    }
    if (!reserve((void **)&wgr_sb.pending, &wgr_sb.pending_capacity, wgr_sb.pending_count, sizeof(wgr_sprite_pending_t),
                 INSTANCES_INITIAL)) {
        return;
    }
    wgr_sb.pending[wgr_sb.pending_count++] = (wgr_sprite_pending_t){
        .quad = *instance, .view = view, .sampler = sampler, .pipeline = pipeline, .material = material};
}

void wgr_sprite_batch_add_2d(const wgr_sprite_quad_t *instance, uint32_t view, uint32_t sampler, wgr_alpha_mode_t mode,
                            wgr_handle_t material)
{
    const int pipeline = mode == WGR_ALPHA_OPAQUE || mode == WGR_ALPHA_MASK ? PIPELINE_2D_OPAQUE
                         : mode == WGR_ALPHA_ADD                           ? PIPELINE_2D_ADD
                                                                          : PIPELINE_2D_BLEND;
    record(instance, view, sampler, pipeline, material); /* 2D keeps its order: never grouped */
}

void wgr_sprite_batch_begin_unordered(void)
{
    wgr_sb.unordered = true;
    wgr_sb.pending_count = 0;
}

/* A group of unordered sprites: one texture, sampler and pipeline. */
typedef struct {
    uint32_t view, sampler;
    int pipeline;
    wgr_handle_t material;
    int count, next; /* sprites in it; where its next one goes in the grouped order */
} wgr_sprite_group_t;

#define MAX_GROUPS 64 /* distinct texture / mode combinations grouped in one pass */

static int compare_groups(const void *lhs, const void *rhs)
{
    const wgr_sprite_group_t *a = lhs, *b = rhs;
    if (a->pipeline != b->pipeline) return a->pipeline < b->pipeline ? -1 : 1;
    if (a->material != b->material) return a->material < b->material ? -1 : 1;
    if (a->view != b->view) return a->view < b->view ? -1 : 1;
    return (a->sampler > b->sampler) - (a->sampler < b->sampler);
}

/* The group a pending sprite belongs to, adding it; -1 when there are too many. */
static bool in_group(const wgr_sprite_group_t *group, const wgr_sprite_pending_t *p)
{
    return group->view == p->view && group->sampler == p->sampler && group->pipeline == p->pipeline &&
           group->material == p->material;
}


static int group_of(wgr_sprite_group_t *groups, int *count, const wgr_sprite_pending_t *p, int hint)
{
    if (hint >= 0 && in_group(&groups[hint], p)) {
        return hint;
    }
    for (int g = 0; g < *count; g++) {
        if (in_group(&groups[g], p)) {
            return g;
        }
    }
    if (*count >= MAX_GROUPS) return -1;
    groups[*count] = (wgr_sprite_group_t){
        .view = p->view, .sampler = p->sampler, .pipeline = p->pipeline, .material = p->material};
    return (*count)++;
}

void wgr_sprite_batch_end_unordered(void)
{
    wgr_sprite_group_t groups[MAX_GROUPS];
    int group_count = 0, hint = -1, placed = 0;

    wgr_sb.unordered = false;
    /* group by texture and mode, keeping each group's sprites in the order they came
       (a counting sort over the few groups: linear, and stable) */
    for (int i = 0; i < wgr_sb.pending_count; i++) {
        const int g = group_of(groups, &group_count, &wgr_sb.pending[i], hint);
        if (g < 0) { /* too many kinds to group: record them as they came */
            group_count = 0;
            break;
        }
        groups[g].count++;
        hint = g;
    }
    if (group_count > 0 && wgr_sb.order_capacity < wgr_sb.pending_count) {
        int *grown = realloc(wgr_sb.order, sizeof(int) * (size_t)wgr_sb.pending_capacity);
        if (grown != NULL) {
            wgr_sb.order = grown;
            wgr_sb.order_capacity = wgr_sb.pending_capacity;
        }
    }
    if (group_count == 0 || wgr_sb.order_capacity < wgr_sb.pending_count) {
        for (int i = 0; i < wgr_sb.pending_count; i++) {
            record(&wgr_sb.pending[i].quad, wgr_sb.pending[i].view, wgr_sb.pending[i].sampler, wgr_sb.pending[i].pipeline,
                   wgr_sb.pending[i].material);
        }
        wgr_sb.pending_count = 0;
        return;
    }
    qsort(groups, (size_t)group_count, sizeof(groups[0]), compare_groups); /* a handful */
    for (int g = 0; g < group_count; g++) {
        groups[g].next = placed;
        placed += groups[g].count;
    }
    hint = -1;
    for (int i = 0; i < wgr_sb.pending_count; i++) {
        const int g = group_of(groups, &group_count, &wgr_sb.pending[i], hint);
        wgr_sb.order[groups[g].next++] = i;
        hint = g;
    }
    for (int k = 0; k < wgr_sb.pending_count; k++) {
        const wgr_sprite_pending_t *p = &wgr_sb.pending[wgr_sb.order[k]];
        record(&p->quad, p->view, p->sampler, p->pipeline, p->material);
    }
    wgr_sb.pending_count = 0;
}

/* No base instance: write the frame's sprites into the data texture, growing it. */
static void flush_pulled(void)
{
    const int rows = (wgr_sb.instance_count + PULLED_SPRITES_PER_ROW - 1) / PULLED_SPRITES_PER_ROW;
    const int max_rows = sg_query_limits().max_image_size_2d;

    if (rows > max_rows) {
        if (!wgr_sb.overflow_logged) {
            log_error("sprites: %d in a frame, more than the sprite texture holds", wgr_sb.instance_count);
            wgr_sb.overflow_logged = true;
        }
        wgr_sb.instance_count = max_rows * PULLED_SPRITES_PER_ROW;
    }
    if (wgr_sb.data_capacity < rows * PULLED_SPRITES_PER_ROW) {
        const int capacity = rows * PULLED_SPRITES_PER_ROW;
        float *grown = realloc(wgr_sb.data, sizeof(float) * 4 * PULLED_TEXELS * (size_t)capacity);
        if (grown == NULL) {
            wgr_sb.instance_count = 0;
            return;
        }
        wgr_sb.data = grown;
        wgr_sb.data_capacity = capacity;
    }
    if (rows > wgr_sb.data_rows) {
        int height = wgr_sb.data_rows > 0 ? wgr_sb.data_rows : 4;
        while (height < rows) height *= 2;
        if (height > max_rows) height = max_rows;
        sg_destroy_view(wgr_sb.data_view);
        sg_destroy_image(wgr_sb.data_image);
        wgr_sb.data_image = sg_make_image(&(sg_image_desc){
            .width = PULLED_WIDTH,
            .height = height,
            .pixel_format = SG_PIXELFORMAT_RGBA32F,
            .usage = {.write_transient = true},
            .label = "wgr-sprite-data",
        });
        wgr_sb.data_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sb.data_image});
        wgr_sb.data_rows = height;
    }
    for (int i = 0; i < wgr_sb.instance_count; i++) {
        const wgr_sprite_quad_t *q = &wgr_sb.instances[i];
        float *t = &wgr_sb.data[(size_t)i * 4 * PULLED_TEXELS]; /* rows are whole sprites */
        const float texels[4 * PULLED_TEXELS] = {
            q->position[0], q->position[1], q->position[2], q->facing,
            q->size[0], q->size[1], q->pivot[0], q->pivot[1],
            q->uv[0], q->uv[1], q->uv[2], q->uv[3],
            q->right[0], q->right[1], q->right[2], 0.0f,
            q->up[0], q->up[1], q->up[2], q->alpha,
            q->color[0] / 255.0f, q->color[1] / 255.0f, q->color[2] / 255.0f, q->color[3] / 255.0f,
        };
        memcpy(t, texels, sizeof(texels));
    }
    sg_write_image_transient(&(sg_write_image_desc){
        .src = {.data = {.ptr = wgr_sb.data, .size = sizeof(float) * 4 * PULLED_WIDTH * (size_t)rows},
                .bytes_per_row = (int)sizeof(float) * 4 * PULLED_WIDTH,
                .bytes_per_slice = (int)sizeof(float) * 4 * PULLED_WIDTH * rows}, /* just the rows in use */
        .dst = {.image = wgr_sb.data_image},
        .size = {.width = PULLED_WIDTH, .height = rows, .num_slices = 1},
    });
}

void wgr_sprite_batch_flush(void)
{
    const size_t bytes = sizeof(wgr_sprite_quad_t) * (size_t)wgr_sb.instance_count;

    if (!wgr_sb.ready || wgr_sb.instance_count == 0) {
        return;
    }
    if (!wgr_sb.base_instance) {
        flush_pulled();
        return;
    }
    if (wgr_sb.instance_count > wgr_sb.instance_buffer_capacity) {
        int capacity = wgr_sb.instance_buffer_capacity > 0 ? wgr_sb.instance_buffer_capacity : INSTANCES_INITIAL;
        while (capacity < wgr_sb.instance_count) capacity *= 2;
        sg_destroy_buffer(wgr_sb.instance_buffer);
        wgr_sb.instance_buffer = sg_make_buffer(&(sg_buffer_desc){
            .size = sizeof(wgr_sprite_quad_t) * (size_t)capacity,
            .usage = {.vertex_buffer = true, .write_transient = true},
            .label = "wgr-sprite-instances",
        });
        wgr_sb.instance_buffer_capacity = capacity;
    }
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .src.data = {.ptr = wgr_sb.instances, .size = bytes},
        .dst.buffer = wgr_sb.instance_buffer,
    });
}

/* Uniform block 0 of a custom shader's sprite programs (wgr_sprite_view in shaders/wgr.glsl). */
typedef struct {
    float view_proj[16];
    float camera_right[4];
    float camera_up[4];
    float upright[4];
    float time[4];
} wgr_sprite_custom_view_t;

/* A batch whose sprites have a custom material: drawn by its shader's sprite program,
 * with libwgrender's blocks and textures and the material's parameters and textures. False
 * when the material or its shader has gone (the batch then draws as usual). */
static bool draw_custom(const wgr_sprite_batch_t *b)
{
    const wgr_material_t *material = wgr_material_get(b->material);
    wgr_shader_t *shader = material != NULL && material->shader != 0 && wgr_shader_hooks.get != NULL
                              ? wgr_shader_hooks.get(material->shader)
                              : NULL;
    const wgr_sprite_camera_t *cam = &wgr_sb.cameras[b->camera];
    const wgr_light_env_t *env = wgr_light_env_get(b->light_env);
    const float time = (float)wgr_get_time();
    const wgr_shader_program_t *program;
    wgr_sprite_custom_view_t view;
    wgr_environment_binding_t environment = {0};
    wgr_shadow_binding_t custom_shadow = {0};
    sg_bindings bind = {0};
    sg_view white, black_cube;
    sg_sampler linear;

    if (shader == NULL) return false;
    program = &shader->programs[wgr_sb.base_instance ? WGR_SHADER_PROGRAM_SPRITE : WGR_SHADER_PROGRAM_SPRITE_PULLED];
    if (shader->sprite_pipelines[0].id == SG_INVALID_ID) {
        make_pipelines(program->shader, shader->sprite_pipelines);
    }
    sg_apply_pipeline(shader->sprite_pipelines[b->pipeline]);

    memcpy(view.view_proj, cam->params.view_proj, sizeof(view.view_proj));
    memcpy(view.camera_right, cam->params.camera_right, sizeof(view.camera_right));
    memcpy(view.camera_up, cam->params.camera_up, sizeof(view.camera_up));
    memcpy(view.upright, cam->params.upright, sizeof(view.upright));
    view.time[0] = time;
    view.time[1] = view.time[2] = view.time[3] = 0.0f;
    sg_apply_uniforms(WGR_SHADER_BLOCK_OBJECT, &SG_RANGE(view));
    if (wgr_environment_hooks.get_binding != NULL) {
        wgr_environment_hooks.get_binding(env != NULL ? env->environment : 0, &environment);
    }
    if (wgr_shadow_hooks.get_binding != NULL) {
        wgr_shadow_hooks.get_binding(b->light_env, &custom_shadow);
    }
    if (program->has_block[WGR_SHADER_BLOCK_FRAME]) {
        /* the scene's lights and environment, as a model's shader gets them */
        wgr_shader_frame_t frame;
        int lights[WGR_MAX_DRAW_LIGHTS];
        memset(&frame, 0, sizeof(frame));
        if (cam->aspect >= 0.0f) { /* 3D: the camera's position (2D has none) */
            frame.camera_time[0] = cam->source.position.x;
            frame.camera_time[1] = cam->source.position.y;
            frame.camera_time[2] = cam->source.position.z;
        }
        frame.camera_time[3] = time;
        /* a sprite's tint is in wgr_color, and its vertex stage writes wgr_tint white */
        frame.output[2] = 1.0f;                                               /* exposure */
        if (env != NULL) {
            frame.output[1] = (float)env->tonemap;
            frame.output[2] = powf(2.0f, env->exposure);
            frame.ambient_count[0] = env->ambient.x;
            frame.ambient_count[1] = env->ambient.y;
            frame.ambient_count[2] = env->ambient.z;
            const int light_count = wgr_light_select(env, b->bounds_min, b->bounds_max, lights, WGR_MAX_DRAW_LIGHTS);
            frame.ambient_count[3] = (float)light_count;
            for (int i = 0; i < light_count; i++) {
                const wgr_scene_light_t *light = &env->lights[lights[i]];
                frame.light_pos_range[i][0] = light->position.x;
                frame.light_pos_range[i][1] = light->position.y;
                frame.light_pos_range[i][2] = light->position.z;
                frame.light_pos_range[i][3] = light->range;
                frame.light_dir_type[i][0] = light->direction.x;
                frame.light_dir_type[i][1] = light->direction.y;
                frame.light_dir_type[i][2] = light->direction.z;
                frame.light_dir_type[i][3] = (float)light->type;
                frame.light_radiance[i][0] = light->radiance.x;
                frame.light_radiance[i][1] = light->radiance.y;
                frame.light_radiance[i][2] = light->radiance.z;
                frame.light_spot[i][0] = light->cos_inner;
                frame.light_spot[i][1] = light->cos_outer;
            }
            if (environment.valid && env->environment_intensity > 0.0f) {
                frame.env[0] = env->environment_intensity;
                frame.env[1] = environment.max_lod;
                frame.env[2] = cosf(env->environment_rotation);
                frame.env[3] = sinf(env->environment_rotation);
                for (int k = 0; k < 9; k++) {
                    frame.sh[k][0] = environment.sh.c[k][0];
                    frame.sh[k][1] = environment.sh.c[k][1];
                    frame.sh[k][2] = environment.sh.c[k][2];
                }
            }
            /* the frame's casting lights, as the built-in shading gets them */
            wgr_shadow_fill_uniforms(&custom_shadow, frame.shadow_mat, frame.shadow_params, frame.shadow_tint,
                                    frame.shadow_extra, frame.shadow_map);
            for (int i = 0; i < light_count; i++) {
                frame.light_spot[i][2] = (float)wgr_shadow_slot_of(&custom_shadow, lights[i]);
            }
        }
        sg_apply_uniforms(WGR_SHADER_BLOCK_FRAME, &SG_RANGE(frame));
    }
    if (program->has_block[WGR_SHADER_BLOCK_FS_PARAMS]) {
        sg_apply_uniforms(WGR_SHADER_BLOCK_FS_PARAMS,
                          &(sg_range){.ptr = material->custom_params,
                                      .size = (size_t)shader->block_size[WGR_SHADER_BLOCK_FS_PARAMS]});
    }
    if (program->has_block[WGR_SHADER_BLOCK_VS_PARAMS]) {
        sg_apply_uniforms(WGR_SHADER_BLOCK_VS_PARAMS,
                          &(sg_range){.ptr = material->custom_params + shader->block_size[WGR_SHADER_BLOCK_FS_PARAMS],
                                      .size = (size_t)shader->block_size[WGR_SHADER_BLOCK_VS_PARAMS]});
    }
    if (!wgr_sb.base_instance && program->has_block[WGR_SHADER_BLOCK_SPRITE_BATCH]) {
        const float first[4] = {(float)b->first, 0.0f, 0.0f, 0.0f};
        sg_apply_uniforms(WGR_SHADER_BLOCK_SPRITE_BATCH, &SG_RANGE(first));
    }

    wgr_shader_hooks.fallbacks(&white, &black_cube, &linear);
    bind.vertex_buffers[0] = wgr_sb.quad;
    if (wgr_sb.base_instance) bind.vertex_buffers[1] = wgr_sb.instance_buffer;
    for (int t = 0; t < shader->texture_count; t++) { /* the material's (a texture not set: white) */
        const wgr_material_texture_t *texture = &material->textures[t];
        sg_view texture_view = white;
        if (texture->texture != 0) wgr_texture_get_binding(texture->texture, &texture_view, NULL, NULL, NULL);
        if (program->view_slot[t] >= 0) bind.views[program->view_slot[t]] = texture_view;
        if (program->sampler_slot[t] >= 0) {
            bind.samplers[program->sampler_slot[t]] =
                wgr_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
        }
    }
    if (program->sprite_view_slot >= 0) bind.views[program->sprite_view_slot] = (sg_view){b->view};
    if (program->sprite_sampler_slot >= 0) bind.samplers[program->sprite_sampler_slot] = (sg_sampler){b->sampler};
    if (program->data_view_slot >= 0) bind.views[program->data_view_slot] = wgr_sb.data_view;
    if (program->data_sampler_slot >= 0) bind.samplers[program->data_sampler_slot] = wgr_sb.data_sampler;
    if (program->shadow_view_slot >= 0) {
        bind.views[program->shadow_view_slot] = custom_shadow.valid ? custom_shadow.map : wgr_sb.no_shadow_view;
    }
    if (program->shadow_sampler_slot >= 0) {
        bind.samplers[program->shadow_sampler_slot] =
            custom_shadow.valid ? custom_shadow.sampler : wgr_sb.no_shadow_sampler;
    }
    if (program->env_view_slot >= 0) {
        bind.views[program->env_view_slot] = environment.cube.id != 0 ? environment.cube : black_cube;
    }
    if (program->env_sampler_slot >= 0) {
        bind.samplers[program->env_sampler_slot] =
            environment.cube_sampler.id != 0 ? environment.cube_sampler : linear;
    }
    if (program->brdf_view_slot >= 0) {
        bind.views[program->brdf_view_slot] = environment.brdf_lut.id != 0 ? environment.brdf_lut : white;
    }
    /* the BRDF table shares the environment's sampler, so bind it only if it has one
       of its own */
    if (program->brdf_sampler_slot >= 0 && program->brdf_sampler_slot != program->env_sampler_slot) {
        bind.samplers[program->brdf_sampler_slot] = environment.lut_sampler.id != 0 ? environment.lut_sampler : linear;
    }
    sg_apply_bindings(&bind);
    sg_apply_scissor_rectf(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3], true);
    if (wgr_sb.base_instance) {
        sg_draw_ex(0, 6, b->count, 0, b->first);
    } else {
        sg_draw(0, 6, b->count);
    }
    return true;
}

/* The shading pipelines and stand-in textures for built-in materials, on first use. */
static void ensure_lit(void)
{
    static const uint32_t white_texel = 0xFFFFFFFFu, flat_normal_texel = 0xFFFF8080u; /* (0.5, 0.5, 1) */
    if (wgr_sb.lit_shader.id != SG_INVALID_ID) return;
    wgr_sb.lit_shader = sg_make_shader(wgr_sb.base_instance ? sprite_quad_lit_shader_desc(shader_backend())
                                                          : sprite_quad_lit_pulled_shader_desc(shader_backend()));
    make_pipelines(wgr_sb.lit_shader, wgr_sb.lit_pipelines);
    wgr_sb.white = sg_make_image(&(sg_image_desc){
        .width = 1, .height = 1, .data.mip_levels[0] = SG_RANGE(white_texel), .label = "wgr-sprite-white"});
    wgr_sb.flat_normal = sg_make_image(&(sg_image_desc){
        .width = 1, .height = 1, .data.mip_levels[0] = SG_RANGE(flat_normal_texel), .label = "wgr-sprite-flat-normal"});
    sg_image_desc cube = {.type = SG_IMAGETYPE_CUBE, .width = 1, .height = 1, .num_slices = 6,
                          .label = "wgr-sprite-black-cube"};
    static const uint32_t black_texels[6] = {0xFF000000u, 0xFF000000u, 0xFF000000u,
                                             0xFF000000u, 0xFF000000u, 0xFF000000u};
    cube.data.mip_levels[0] = (sg_range){.ptr = black_texels, .size = sizeof(black_texels)};
    wgr_sb.black_cube = sg_make_image(&cube);
    wgr_sb.white_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sb.white});
    wgr_sb.flat_normal_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sb.flat_normal});
    wgr_sb.black_cube_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sb.black_cube});
    wgr_sb.lit_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR, .label = "wgr-sprite-lit"});
    wgr_sb.no_shadow = sg_make_image(&(sg_image_desc){
        .type = SG_IMAGETYPE_ARRAY, .usage.depth_stencil_attachment = true, .width = 1, .height = 1,
        .num_slices = 1, .pixel_format = SG_PIXELFORMAT_DEPTH, .sample_count = 1, .label = "wgr-sprite-no-shadow"});
    wgr_sb.no_shadow_view = sg_make_view(&(sg_view_desc){.texture.image = wgr_sb.no_shadow});
    wgr_sb.no_shadow_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR, .mag_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE, .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
        .compare = SG_COMPAREFUNC_LESS_EQUAL, .label = "wgr-sprite-no-shadow-smp"});
}

/* A batch of sprites with a built-in material: libwgrender's model shading, lit by the
 * lights this batch's sprites are nearest (chosen once, from their bounds). The
 * sprite's own texture is the base color; the material's maps and factors are the
 * rest. */
static bool draw_lit(const wgr_sprite_batch_t *b)
{
    const wgr_material_t *material = wgr_material_get(b->material);
    const wgr_light_env_t *env = wgr_light_env_get(b->light_env);
    const wgr_sprite_camera_t *cam = &wgr_sb.cameras[b->camera];
    const bool lit = env != NULL && material != NULL && material->shading == WGR_MATERIAL_PBR;
    int lights[WGR_MAX_DRAW_LIGHTS];
    int light_count = 0;
    sprite_fs_params_t params;
    sprite_fs_scene_t scene;
    sprite_fs_lights_t light_block;
    wgr_environment_binding_t environment = {0};
    wgr_shadow_binding_t shadow = {0};
    sg_bindings bind = {0};

    if (material == NULL) return false; /* released while the batch waited */
    ensure_lit();
    memset(&params, 0, sizeof(params));
    memset(&scene, 0, sizeof(scene));
    memset(&light_block, 0, sizeof(light_block));
    if (wgr_environment_hooks.get_binding != NULL) {
        wgr_environment_hooks.get_binding(lit ? env->environment : 0, &environment);
    }

    params.u_base_color[0] = material->base_color[0]; /* the sprite's tint is its vertex color */
    params.u_base_color[1] = material->base_color[1];
    params.u_base_color[2] = material->base_color[2];
    params.u_base_color[3] = material->base_color[3];
    params.u_emissive[0] = material->emissive[0];
    params.u_emissive[1] = material->emissive[1];
    params.u_emissive[2] = material->emissive[2];
    params.u_emissive[3] = material->textures[WGR_MATERIAL_TEXTURE_NORMAL].texture != 0 ? material->normal_scale : 0.0f;
    params.u_pbr[0] = material->metallic;
    params.u_pbr[1] = material->roughness;
    params.u_pbr[2] = material->occlusion_strength;
    params.u_pbr[3] = lit ? 1.0f : 0.0f;
    params.u_material[0] = material->alpha_mode == WGR_ALPHA_MASK ? material->alpha_cutoff : 0.0f;
    params.u_material[2] = lit ? 1.0f : 0.0f; /* sprites receive shadows when they're lit */
    for (int t = 0; t < WGR_MATERIAL_TEXTURE_COUNT; t++) { /* the material's texture transforms */
        float m[6];
        wgr_material_uv_matrix(&material->textures[t], m);
        params.u_uv_row0[t][0] = m[0], params.u_uv_row0[t][1] = m[1], params.u_uv_row0[t][2] = m[2];
        params.u_uv_row1[t][0] = m[3], params.u_uv_row1[t][1] = m[4], params.u_uv_row1[t][2] = m[5];
    }
    scene.u_camera_pos[0] = cam->source.position.x;
    scene.u_camera_pos[1] = cam->source.position.y;
    scene.u_camera_pos[2] = cam->source.position.z;
    scene.u_tonemap[0] = env != NULL ? (float)env->tonemap : 0.0f;
    scene.u_tonemap[1] = env != NULL ? powf(2.0f, env->exposure) : 1.0f;
    if (wgr_shadow_hooks.get_binding != NULL) {
        wgr_shadow_hooks.get_binding(b->light_env, &shadow);
    }
    if (lit) {
        scene.u_ambient[0] = env->ambient.x;
        scene.u_ambient[1] = env->ambient.y;
        scene.u_ambient[2] = env->ambient.z;
        if (environment.valid && env->environment_intensity > 0.0f) {
            scene.u_env[0] = env->environment_intensity;
            scene.u_env[1] = environment.max_lod;
            scene.u_env[2] = cosf(env->environment_rotation);
            scene.u_env[3] = sinf(env->environment_rotation);
            for (int k = 0; k < 9; k++) {
                scene.u_sh[k][0] = environment.sh.c[k][0];
                scene.u_sh[k][1] = environment.sh.c[k][1];
                scene.u_sh[k][2] = environment.sh.c[k][2];
            }
        }
        light_count = wgr_light_select(env, b->bounds_min, b->bounds_max, lights, WGR_MAX_DRAW_LIGHTS);
        params.u_material[1] = (float)light_count;
        /* the frame's casting lights, a layer of the map each */
        wgr_shadow_fill_uniforms(&shadow, scene.u_shadow_mat, scene.u_shadow_params, scene.u_shadow_tint,
                                scene.u_shadow_extra, scene.u_shadow_map);
        for (int i = 0; i < light_count; i++) {
            light_block.u_light_spot[i][2] = (float)wgr_shadow_slot_of(&shadow, lights[i]);
        }
    }

    sg_apply_pipeline(wgr_sb.lit_pipelines[b->pipeline]);
    sg_apply_uniforms(UB_sprite_vs_params, &SG_RANGE(wgr_sb.cameras[b->camera].params));
    sg_apply_uniforms(UB_sprite_fs_params, &SG_RANGE(params));
    sg_apply_uniforms(UB_sprite_fs_scene, &SG_RANGE(scene));
    sg_apply_uniforms(UB_sprite_fs_lights, &SG_RANGE(light_block));
    bind.vertex_buffers[0] = wgr_sb.quad;
    if (wgr_sb.base_instance) {
        bind.vertex_buffers[1] = wgr_sb.instance_buffer;
    } else {
        const sprite_vs_batch_lit_t batch_params = {.batch_lit = {(float)b->first, 0.0f, 0.0f, 0.0f}};
        sg_apply_uniforms(UB_sprite_vs_batch_lit, &SG_RANGE(batch_params));
        bind.views[VIEW_sprite_sprite_data_lit] = wgr_sb.data_view;
        bind.samplers[SMP_sprite_sprite_data_lit_smp] = wgr_sb.data_sampler;
    }
    /* the sprite's texture is the base color; the material's maps are the rest */
    bind.views[VIEW_sprite_base_color_tex] = (sg_view){b->view};
    bind.samplers[SMP_sprite_base_color_smp] = (sg_sampler){b->sampler};
    static const int slots[4] = {WGR_MATERIAL_TEXTURE_METALLIC_ROUGHNESS, WGR_MATERIAL_TEXTURE_NORMAL,
                                 WGR_MATERIAL_TEXTURE_OCCLUSION, WGR_MATERIAL_TEXTURE_EMISSIVE};
    const int views[4] = {VIEW_sprite_metallic_roughness_tex, VIEW_sprite_normal_tex, VIEW_sprite_occlusion_tex,
                          VIEW_sprite_emissive_tex};
    const int samplers[4] = {SMP_sprite_metallic_roughness_smp, SMP_sprite_normal_smp, SMP_sprite_occlusion_smp,
                             SMP_sprite_emissive_smp};
    for (int i = 0; i < 4; i++) {
        const wgr_material_texture_t *texture = &material->textures[slots[i]];
        sg_view view = slots[i] == WGR_MATERIAL_TEXTURE_NORMAL ? wgr_sb.flat_normal_view : wgr_sb.white_view;
        if (texture->texture != 0) wgr_texture_get_binding(texture->texture, &view, NULL, NULL, NULL);
        bind.views[views[i]] = view;
        bind.samplers[samplers[i]] =
            wgr_texture_sampler(texture->wrap_u, texture->wrap_v, texture->filter, texture->mipmaps);
    }
    /* without the environment module there's none to bind: black, and zero intensity */
    bind.views[VIEW_sprite_env_tex] = environment.cube.id != 0 ? environment.cube : wgr_sb.black_cube_view;
    bind.samplers[SMP_sprite_env_smp] =
        environment.cube_sampler.id != 0 ? environment.cube_sampler : wgr_sb.lit_sampler;
    bind.views[VIEW_sprite_brdf_tex] = environment.brdf_lut.id != 0 ? environment.brdf_lut : wgr_sb.white_view;
    bind.views[VIEW_sprite_shadow_tex] = shadow.valid ? shadow.map : wgr_sb.no_shadow_view;
    bind.samplers[SMP_sprite_shadow_smp] = shadow.valid ? shadow.sampler : wgr_sb.no_shadow_sampler;
    bind.samplers[SMP_sprite_brdf_smp] =
        environment.lut_sampler.id != 0 ? environment.lut_sampler : wgr_sb.lit_sampler;
    sg_apply_bindings(&bind);
    sg_apply_scissor_rectf(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3], true);
    if (wgr_sb.base_instance) {
        sg_draw_ex(0, 6, b->count, 0, b->first);
    } else {
        sg_draw(0, 6, b->count);
    }
    return true;
}

void wgr_sprite_batch_draw(int batch, bool follows)
{
    const wgr_sprite_batch_t *b, *before;

    if (!wgr_sb.ready || batch < 0 || batch >= wgr_sb.batch_count) {
        return;
    }
    b = &wgr_sb.batches[batch];
    if (b->count == 0) {
        return;
    }
    if (b->lit && draw_lit(b)) {
        wgr_sb.last_drawn = -1; /* the next batch applies everything again */
        return;
    }
    if (b->material != 0 && !b->lit && draw_custom(b)) {
        wgr_sb.last_drawn = -1; /* the next batch applies everything again */
        return;
    }
    /* right after another batch, only what differs is applied again (sorted sprites
       from several textures make long runs of small batches) */
    before = follows && wgr_sb.last_drawn >= 0 ? &wgr_sb.batches[wgr_sb.last_drawn] : NULL;
    wgr_sb.last_drawn = batch;
    if (before == NULL || before->pipeline != b->pipeline) {
        sg_apply_pipeline(wgr_sb.pipelines[b->pipeline]);
        before = NULL; /* a new pipeline needs its uniforms again */
    }
    /* every batch binds the same instances (base instance) or sprite texture (read by
       index), so consecutive batches only change the texture and where they start */
    if (wgr_sb.base_instance) {
        sg_apply_bindings(&(sg_bindings){
            .vertex_buffers = {wgr_sb.quad, wgr_sb.instance_buffer},
            .views[VIEW_sprite_tex] = {.id = b->view},
            .samplers[SMP_sprite_smp] = {.id = b->sampler},
        });
    } else {
        sg_apply_bindings(&(sg_bindings){
            .vertex_buffers[0] = wgr_sb.quad,
            .views[VIEW_sprite_tex] = {.id = b->view},
            .samplers[SMP_sprite_smp] = {.id = b->sampler},
            .views[VIEW_sprite_sprite_data] = wgr_sb.data_view,
            .samplers[SMP_sprite_sprite_data_smp] = wgr_sb.data_sampler,
        });
    }
    if (before == NULL || before->camera != b->camera) {
        sg_apply_uniforms(UB_sprite_vs_params, &SG_RANGE(wgr_sb.cameras[b->camera].params));
    }
    /* the clip in effect when the sprites were drawn; it stays for the sokol_gl layer
       that follows, which was recorded under the same clip */
    if (before == NULL || memcmp(before->scissor, b->scissor, sizeof(b->scissor)) != 0) {
        sg_apply_scissor_rectf(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3], true);
    }
    if (wgr_sb.base_instance) {
        sg_draw_ex(0, 6, b->count, 0, b->first);
    } else { /* the shader reads sprites first .. first + count - 1 */
        const sprite_vs_batch_t batch_params = {.batch = {(float)b->first, 0.0f, 0.0f, 0.0f}};
        sg_apply_uniforms(UB_sprite_vs_batch, &SG_RANGE(batch_params));
        sg_draw(0, 6, b->count);
    }
}

void wgr_sprite_batch_end_frame(void)
{
    wgr_sb.instance_count = 0;
    wgr_sb.batch_count = 0;
    wgr_sb.camera_count = 0;
    wgr_sb.state_3d.valid = false; /* their camera uniforms are gone */
    wgr_sb.state_2d.valid = false;
    wgr_sb.last_drawn = -1;
}

int wgr_sprite_batch_count(void)
{
    return wgr_sb.batch_count;
}

/* Part of the runtime when sprites are (sprite3d and sprite2d start and stop it): the
 * frame's instances go up before any pass, and start over after (internal/wgr_module.h). */
static wgr_module_t wgr_sprite_batch_module = {.name = "sprite_batch", .order = 55, .flush = wgr_sprite_batch_flush,
                                             .end_frame = wgr_sprite_batch_end_frame};
WGR_MODULE(wgr_sprite_batch_module)
