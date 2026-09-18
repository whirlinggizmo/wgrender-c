#include "internal/sk_sprite_batch.h"

#include <stdlib.h>
#include <string.h>

#include "internal/sk_camera3d.h"
#include "internal/sk_math.h"
#include "internal/sk_render.h"
#include "internal/sk_shaders.h"
#include "internal/sk_sprite3d.h"
#include "sk_logger.h"

/* Instanced sprites (docs/PLAN-sprites.md). During the frame, sprites are appended to
 * one instance array in call order and grouped into batches: consecutive sprites
 * with the same texture, camera, clip, depth mode and pass. Each batch is a render
 * command. Before the passes, the instances go up in one buffer update; each batch
 * then draws its range with one instanced draw. */

#define INSTANCES_INITIAL 1024
#define BATCHES_INITIAL 64
#define CAMERAS_INITIAL 8

enum { PIPELINE_DEPTH_WRITE, PIPELINE_NO_DEPTH_WRITE, PIPELINE_COUNT };

typedef struct {
    uint32_t view, sampler;
    int pipeline;
    int camera; /* index into the frame's camera uniforms */
    int pass;
    float scissor[4]; /* framebuffer pixels: x, y, width, height (the clip, or the whole target) */
    int first, count;
} sk_sprite_batch_t;

/* A camera as the shader needs it, and what it was made from (to reuse it). */
typedef struct {
    sk_camera3d_t source;
    float aspect;
    sprite_vs_params_t params;
} sk_sprite_camera_t;

static struct {
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer quad;
    sg_buffer instance_buffer;
    int instance_buffer_capacity;
    sk_sprite_quad_t *instances;
    int instance_count, instance_capacity;
    sk_sprite_batch_t *batches;
    int batch_count, batch_capacity;
    sk_sprite_camera_t *cameras;
    int camera_count, camera_capacity;
    bool overflow_logged;
    int last_drawn; /* the batch drawn last (replay), whose state may still be applied */
    /* the camera, pass and scissor sprites are drawn with now, until the render state
       or a camera changes */
    struct {
        bool valid;
        unsigned render_revision, camera_revision;
        int camera, pass;
        float scissor[4];
    } state;
} sk_sb;

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
        if (!sk_sb.overflow_logged) {
            log_error("sprites: out of memory");
            sk_sb.overflow_logged = true;
        }
        return false;
    }
    *items = moved;
    *capacity = grown;
    return true;
}

void sk_sprite_batch_init(void)
{
    /* two triangles: the quad's corners, x right, y up */
    static const float corners[12] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
    sg_pipeline_desc desc = {
        .layout = {
            .buffers[1] = {.step_func = SG_VERTEXSTEP_PER_INSTANCE},
            .attrs = {
                [ATTR_sprite_quad_corner] = {.format = SG_VERTEXFORMAT_FLOAT2, .buffer_index = 0},
                [ATTR_sprite_quad_inst_pos] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_size] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_uv] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_quad_inst_right] = {.format = SG_VERTEXFORMAT_FLOAT3, .buffer_index = 1},
                [ATTR_sprite_quad_inst_up] = {.format = SG_VERTEXFORMAT_FLOAT3, .buffer_index = 1},
                [ATTR_sprite_quad_inst_color] = {.format = SG_VERTEXFORMAT_UBYTE4N, .buffer_index = 1},
            },
        },
        /* like sokol_gl's 3D pipelines (sk_render.c): blended, depth-tested, no culling */
        .depth = {.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true},
        .colors[0].blend = {
            .enabled = true,
            .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
            .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .src_factor_alpha = SG_BLENDFACTOR_ONE,
            .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        },
        .label = "sk-sprite",
    };

    memset(&sk_sb, 0, sizeof(sk_sb));
    sk_sb.shader = sg_make_shader(sprite_quad_shader_desc(shader_backend()));
    desc.shader = sk_sb.shader;
    sk_sb.pipelines[PIPELINE_DEPTH_WRITE] = sg_make_pipeline(&desc);
    desc.depth.write_enabled = false;
    sk_sb.pipelines[PIPELINE_NO_DEPTH_WRITE] = sg_make_pipeline(&desc);
    sk_sb.last_drawn = -1;
    sk_sb.quad = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(corners), .label = "sk-sprite-quad"});
    sk_sb.ready = true;
}

void sk_sprite_batch_deinit(void)
{
    if (!sk_sb.ready) {
        return;
    }
    sg_destroy_buffer(sk_sb.instance_buffer);
    sg_destroy_buffer(sk_sb.quad);
    for (int i = 0; i < PIPELINE_COUNT; i++) {
        sg_destroy_pipeline(sk_sb.pipelines[i]);
    }
    sg_destroy_shader(sk_sb.shader);
    free(sk_sb.instances);
    free(sk_sb.batches);
    free(sk_sb.cameras);
    memset(&sk_sb, 0, sizeof(sk_sb));
}

/* The frame's uniforms for the active camera in the current pass, made once and
 * reused while the camera and the target's aspect stay the same; -1 if there's none. */
static int current_camera(void)
{
    sk_camera3d_t cam;
    const vec2_t size = sk_render_target_size();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
    sk_sprite_camera_t *c;
    vec3_t right, up, upright, unused;

    if (!sk_camera3d_get_active_data(&cam)) {
        return -1;
    }
    if (sk_sb.camera_count > 0) {
        c = &sk_sb.cameras[sk_sb.camera_count - 1];
        if (c->aspect == aspect && memcmp(&c->source, &cam, sizeof(cam)) == 0) {
            return sk_sb.camera_count - 1;
        }
    }
    if (!reserve((void **)&sk_sb.cameras, &sk_sb.camera_capacity, sk_sb.camera_count, sizeof(sk_sprite_camera_t),
                 CAMERAS_INITIAL)) {
        return -1;
    }
    c = &sk_sb.cameras[sk_sb.camera_count];
    c->source = cam;
    c->aspect = aspect;
    const sk_mat4_t view_proj = sk_mat4_mul(sk_camera3d_projection(&cam, aspect), sk_camera3d_view(&cam));
    memcpy(c->params.view_proj, view_proj.m, sizeof(c->params.view_proj));
    /* the billboard axes, exactly as picking works them out */
    sk_sprite3d_facing_basis(SK_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
    sk_sprite3d_facing_basis(SK_SPRITE3D_FACING_CAMERA_FIXED_Y, (vec3_t){0, 0, 0}, &cam, &upright, &unused);
    memcpy(c->params.camera_right, (float[4]){right.x, right.y, right.z, 0.0f}, sizeof(c->params.camera_right));
    memcpy(c->params.camera_up, (float[4]){up.x, up.y, up.z, 0.0f}, sizeof(c->params.camera_up));
    memcpy(c->params.upright, (float[4]){upright.x, upright.y, upright.z, 0.0f}, sizeof(c->params.upright));
    return sk_sb.camera_count++;
}

/* The camera uniforms, pass and scissor for sprites drawn now; false without a camera. */
static bool current_state(void)
{
    const unsigned render_revision = sk_render_state_revision(), camera_revision = sk_camera3d_revision();
    float x, y, w, h, scale;

    if (sk_sb.state.valid && sk_sb.state.render_revision == render_revision &&
        sk_sb.state.camera_revision == camera_revision) {
        return true;
    }
    sk_sb.state.camera = current_camera();
    if (sk_sb.state.camera < 0) {
        sk_sb.state.valid = false;
        return false;
    }
    sk_sb.state.pass = sk_render_current_pass();
    sk_render_get_clip(&x, &y, &w, &h); /* the whole target when nothing is pushed */
    scale = sk_render_pixel_scale();
    sk_sb.state.scissor[0] = x * scale, sk_sb.state.scissor[1] = y * scale;
    sk_sb.state.scissor[2] = w * scale, sk_sb.state.scissor[3] = h * scale;
    sk_sb.state.render_revision = render_revision;
    sk_sb.state.camera_revision = camera_revision;
    sk_sb.state.valid = true;
    return true;
}

void sk_sprite_batch_add_3d(const sk_sprite_quad_t *instance, uint32_t view, uint32_t sampler, bool depth_write)
{
    const int pipeline = depth_write ? PIPELINE_DEPTH_WRITE : PIPELINE_NO_DEPTH_WRITE;
    sk_sprite_batch_t *last;

    if (!sk_sb.ready || !current_state()) {
        return;
    }
    if (!reserve((void **)&sk_sb.instances, &sk_sb.instance_capacity, sk_sb.instance_count, sizeof(sk_sprite_quad_t),
                 INSTANCES_INITIAL)) {
        return;
    }
    last = sk_sb.batch_count > 0 ? &sk_sb.batches[sk_sb.batch_count - 1] : NULL;
    /* extend the open batch when nothing else was drawn since and everything matches */
    if (last == NULL || last->view != view || last->sampler != sampler || last->pipeline != pipeline ||
        last->camera != sk_sb.state.camera || last->pass != sk_sb.state.pass ||
        memcmp(last->scissor, sk_sb.state.scissor, sizeof(last->scissor)) != 0 ||
        !sk_render_sprites_open(sk_sb.batch_count - 1)) {
        if (!reserve((void **)&sk_sb.batches, &sk_sb.batch_capacity, sk_sb.batch_count, sizeof(sk_sprite_batch_t),
                     BATCHES_INITIAL)) {
            return;
        }
        if (!sk_render_submit_sprites(sk_sb.batch_count)) {
            return; /* out of render commands (logged there) */
        }
        last = &sk_sb.batches[sk_sb.batch_count++];
        *last = (sk_sprite_batch_t){
            .view = view,
            .sampler = sampler,
            .pipeline = pipeline,
            .camera = sk_sb.state.camera,
            .pass = sk_sb.state.pass,
            .first = sk_sb.instance_count,
        };
        memcpy(last->scissor, sk_sb.state.scissor, sizeof(last->scissor));
    }
    sk_sb.instances[sk_sb.instance_count++] = *instance;
    last->count++;
}

void sk_sprite_batch_flush(void)
{
    const size_t bytes = sizeof(sk_sprite_quad_t) * (size_t)sk_sb.instance_count;

    if (!sk_sb.ready || sk_sb.instance_count == 0) {
        return;
    }
    if (sk_sb.instance_count > sk_sb.instance_buffer_capacity) {
        int capacity = sk_sb.instance_buffer_capacity > 0 ? sk_sb.instance_buffer_capacity : INSTANCES_INITIAL;
        while (capacity < sk_sb.instance_count) capacity *= 2;
        sg_destroy_buffer(sk_sb.instance_buffer);
        sk_sb.instance_buffer = sg_make_buffer(&(sg_buffer_desc){
            .size = sizeof(sk_sprite_quad_t) * (size_t)capacity,
            .usage = {.vertex_buffer = true, .write_transient = true},
            .label = "sk-sprite-instances",
        });
        sk_sb.instance_buffer_capacity = capacity;
    }
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .src.data = {.ptr = sk_sb.instances, .size = bytes},
        .dst.buffer = sk_sb.instance_buffer,
    });
}

void sk_sprite_batch_draw(int batch, bool follows)
{
    const sk_sprite_batch_t *b, *before;

    if (!sk_sb.ready || batch < 0 || batch >= sk_sb.batch_count) {
        return;
    }
    b = &sk_sb.batches[batch];
    if (b->count == 0) {
        return;
    }
    /* right after another batch, only what differs is applied again (sorted sprites
       from several textures make long runs of small batches) */
    before = follows && sk_sb.last_drawn >= 0 ? &sk_sb.batches[sk_sb.last_drawn] : NULL;
    sk_sb.last_drawn = batch;
    if (before == NULL || before->pipeline != b->pipeline) {
        sg_apply_pipeline(sk_sb.pipelines[b->pipeline]);
        before = NULL; /* a new pipeline needs its uniforms again */
    }
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers = {sk_sb.quad, sk_sb.instance_buffer},
        .vertex_buffer_offsets[1] = (int)(sizeof(sk_sprite_quad_t) * (size_t)b->first),
        .views[VIEW_sprite_tex] = {.id = b->view},
        .samplers[SMP_sprite_smp] = {.id = b->sampler},
    });
    if (before == NULL || before->camera != b->camera) {
        sg_apply_uniforms(UB_sprite_vs_params, &SG_RANGE(sk_sb.cameras[b->camera].params));
    }
    /* the clip in effect when the sprites were drawn; it stays for the sokol_gl layer
       that follows, which was recorded under the same clip */
    if (before == NULL || memcmp(before->scissor, b->scissor, sizeof(b->scissor)) != 0) {
        sg_apply_scissor_rectf(b->scissor[0], b->scissor[1], b->scissor[2], b->scissor[3], true);
    }
    sg_draw(0, 6, b->count);
}

void sk_sprite_batch_end_frame(void)
{
    sk_sb.instance_count = 0;
    sk_sb.batch_count = 0;
    sk_sb.camera_count = 0;
    sk_sb.state.valid = false; /* its camera uniforms are gone */
    sk_sb.last_drawn = -1;
}
