#include "sk_emitter2d.h"
#include "sk_emitter3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_camera3d.h"
#include "internal/sk_color.h"
#include "internal/sk_emitter.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_math.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_shaders.h"
#include "internal/sk_sprite3d.h"
#include "internal/sk_texture.h"
#include "sk_color.h"
#include "sk_logger.h"
#include "sk_window.h"

/* Particle emitters (docs/PLAN-sprites.md, step 4). A particle is a birth record:
 * where and when it was born, how fast it went, how long it lives, its size scale,
 * spin and starting angle. The CPU decides those at birth (within the emitter's
 * ranges) and never touches the particle again; the shader (vs_particle in
 * sk_sprite.glsl) works out where it is, how big, what color and how turned from its
 * age. Each emitter keeps its particles in a ring, newest last, and a draw copies the
 * ones that may still be alive into the frame's particle buffer and records one
 * instanced draw, replayed through a render callback so it keeps its place among the
 * frame's other drawing. */

#define EMITTERS_INITIAL 16
#define DEFAULT_MAX 1024
#define MAX_PARTICLES 65536    /* per emitter */
#define REBASE_SECONDS 4096.0f /* the shader's time is a float: keep it small */
#define PARTICLES_INITIAL 1024
#define DRAWS_INITIAL 16
#define TAU 6.28318530718f

/* One particle as the shader reads it (per-instance data, 48 bytes). */
typedef struct {
    float born[4];   /* xyz where, w when (the emitter's clock) */
    float motion[4]; /* xyz velocity, w life (seconds) */
    float shape[4];  /* x size scale, y spin (radians / s), z angle at birth, w unused */
} sk_particle_t;

typedef struct {
    bool two_d;
    bool visible;
    bool emitting;
    sk_handle_t texture;
    float source[4]; /* texture pixels; width or height <= 0: the whole texture */
    vec3_t position;
    vec3_t previous;    /* where it was at the last update: steady spawns spread along the way */
    vec3_t movement_velocity; /* how fast it moved over the last update */
    bool placed;        /* positioned once (the first position doesn't count as a move) */
    float rate;      /* per second */
    float owed;      /* particles due but not yet spawned (fractions carry over) */
    int max;
    sk_particle_t *ring;
    int next;        /* where the next particle goes */
    int live;        /* particles in the window: the `live` before `next` may be alive */
    float life_min, life_max;
    vec3_t box;      /* spawn box half sizes */
    vec3_t velocity; /* direction x speed */
    float spread, speed_variance;
    vec3_t gravity;
    float drag;         /* per second */
    float stretch;      /* seconds of motion */
    float inherit;      /* of the emitter's own velocity */
    float size_start, size_end, size_variance;
    sk_color_t color_start, color_end;
    float spin_min, spin_max;
    sk_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    uint32_t rng;
    float time;      /* the emitter's clock, seconds (rebased now and then) */
} sk_emitter_t;

/* One emitter's draw this frame. */
typedef struct {
    int pipeline;
    uint32_t view, sampler;
    int first, count; /* its particles in the frame's buffer */
    float scissor[4];
    sprite_particle_params_t params;
} sk_particle_draw_t;

enum {
    PIPELINE_3D_OPAQUE, /* opaque and masked: depth written */
    PIPELINE_3D_BLEND,  /* blended and added: depth tested, not written */
    PIPELINE_3D_ADD,
    PIPELINE_2D_OPAQUE, /* 2D: no depth */
    PIPELINE_2D_BLEND,
    PIPELINE_2D_ADD,
    PIPELINE_COUNT
};

static sk_emitter_t *sk_emitters3d;
static sk_emitter_t *sk_emitters2d;
static sk_handle_pool_t sk_emitter3d_pool;
static sk_handle_pool_t sk_emitter2d_pool;

static struct {
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer quad;
    sg_buffer buffer;
    int buffer_capacity;
    sk_particle_t *particles; /* the frame's, for all emitters */
    int particle_count, particle_capacity;
    sk_particle_draw_t *draws;
    int draw_count, draw_capacity;
    uint32_t next_seed;
} sk_px;

/* ------------------------------------------------------------ handles ---- */

static sk_emitter_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (sk_handle_pool_resolve(&sk_emitter3d_pool, handle, &index)) return &sk_emitters3d[index];
    if (sk_handle_pool_resolve(&sk_emitter2d_pool, handle, &index)) return &sk_emitters2d[index];
    if (handle != 0) log_warn("Invalid emitter handle (%u)", (unsigned int)handle);
    return NULL;
}

/* An emitter of the kind a function is for (3D functions refuse 2D emitters). */
static sk_emitter_t *resolve_kind(sk_handle_t handle, bool two_d)
{
    sk_emitter_t *emitter_ptr = resolve(handle);
    return emitter_ptr != NULL && emitter_ptr->two_d == two_d ? emitter_ptr : NULL;
}

/* ------------------------------------------------------------ random ---- */

static float random01(sk_emitter_t *emitter_ptr)
{
    uint32_t x = emitter_ptr->rng; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    emitter_ptr->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static float random_between(sk_emitter_t *emitter_ptr, float a, float b)
{
    return a + (b - a) * random01(emitter_ptr);
}

static float signed_random(sk_emitter_t *emitter_ptr)
{
    return random01(emitter_ptr) * 2.0f - 1.0f;
}

/* ------------------------------------------------------------ spawning ---- */

/* A velocity for a new particle: the emitter's, turned within its spread and scaled
 * within its speed variance. */
static vec3_t birth_velocity(sk_emitter_t *emitter_ptr)
{
    const vec3_t v = emitter_ptr->velocity;
    const float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    float speed;
    vec3_t dir;

    if (length <= 0.0f) {
        return (vec3_t){0, 0, 0};
    }
    speed = length * fmaxf(0.0f, 1.0f + emitter_ptr->speed_variance * signed_random(emitter_ptr));
    if (emitter_ptr->two_d) { /* turned up to `spread` either way */
        const float angle = atan2f(v.y, v.x) + emitter_ptr->spread * signed_random(emitter_ptr);
        return (vec3_t){cosf(angle) * speed, sinf(angle) * speed, 0.0f};
    }
    dir = (vec3_t){v.x / length, v.y / length, v.z / length};
    if (emitter_ptr->spread > 0.0f) { /* uniformly within a cone around the direction */
        const float cos_theta = 1.0f - random01(emitter_ptr) * (1.0f - cosf(fminf(emitter_ptr->spread, 3.14159265f)));
        const float sin_theta = sqrtf(fmaxf(0.0f, 1.0f - cos_theta * cos_theta));
        const float phi = TAU * random01(emitter_ptr);
        const vec3_t helper = fabsf(dir.y) < 0.99f ? (vec3_t){0, 1, 0} : (vec3_t){1, 0, 0};
        vec3_t u = sk_v3_norm(sk_v3_cross(helper, dir));
        const vec3_t w = sk_v3_cross(dir, u);
        dir = (vec3_t){dir.x * cos_theta + (u.x * cosf(phi) + w.x * sinf(phi)) * sin_theta,
                       dir.y * cos_theta + (u.y * cosf(phi) + w.y * sinf(phi)) * sin_theta,
                       dir.z * cos_theta + (u.z * cosf(phi) + w.z * sinf(phi)) * sin_theta};
    }
    return (vec3_t){dir.x * speed, dir.y * speed, dir.z * speed};
}

/* A new particle, born at `when` on the emitter's clock around `at` (replacing the
 * oldest when the ring is full). */
static void spawn(sk_emitter_t *emitter_ptr, float when, vec3_t at)
{
    const vec3_t v = birth_velocity(emitter_ptr);
    const vec3_t box = emitter_ptr->box;
    const vec3_t moving = sk_v3_scale(emitter_ptr->movement_velocity, emitter_ptr->inherit);
    sk_particle_t *p = &emitter_ptr->ring[emitter_ptr->next];

    *p = (sk_particle_t){
        .born = {at.x + box.x * signed_random(emitter_ptr), at.y + box.y * signed_random(emitter_ptr),
                 emitter_ptr->two_d ? 0.0f : at.z + box.z * signed_random(emitter_ptr), when},
        .motion = {v.x + moving.x, v.y + moving.y, emitter_ptr->two_d ? 0.0f : v.z + moving.z,
                   random_between(emitter_ptr, emitter_ptr->life_min, emitter_ptr->life_max)},
        .shape = {fmaxf(0.0f, 1.0f + emitter_ptr->size_variance * signed_random(emitter_ptr)),
                  random_between(emitter_ptr, emitter_ptr->spin_min, emitter_ptr->spin_max),
                  TAU * random01(emitter_ptr), 0.0f},
    };
    emitter_ptr->next = (emitter_ptr->next + 1) % emitter_ptr->max;
    if (emitter_ptr->live < emitter_ptr->max) emitter_ptr->live++;
}

static int oldest(const sk_emitter_t *emitter_ptr)
{
    return (emitter_ptr->next - emitter_ptr->live + emitter_ptr->max) % emitter_ptr->max;
}

/* Drop particles from the old end of the window once none of them can be alive. */
static void retire(sk_emitter_t *emitter_ptr)
{
    while (emitter_ptr->live > 0) {
        const sk_particle_t *p = &emitter_ptr->ring[oldest(emitter_ptr)];
        if (emitter_ptr->time - p->born[3] < p->motion[3]) break;
        emitter_ptr->live--;
    }
}

static void update_emitter(sk_emitter_t *emitter_ptr, float dt)
{
    const vec3_t from = emitter_ptr->previous;
    const vec3_t moved = sk_v3_sub(emitter_ptr->position, from);
    int due;

    emitter_ptr->time += dt;
    if (emitter_ptr->time > REBASE_SECONDS) { /* keep the clock small for the shader's floats */
        for (int i = 0, at = oldest(emitter_ptr); i < emitter_ptr->live; i++, at = (at + 1) % emitter_ptr->max) {
            emitter_ptr->ring[at].born[3] -= REBASE_SECONDS;
        }
        emitter_ptr->time -= REBASE_SECONDS;
    }
    retire(emitter_ptr);
    emitter_ptr->previous = emitter_ptr->position;
    if (dt > 0.0f) emitter_ptr->movement_velocity = sk_v3_scale(moved, 1.0f / dt);
    if (!emitter_ptr->emitting || emitter_ptr->rate <= 0.0f || dt <= 0.0f) {
        emitter_ptr->owed = 0.0f;
        return;
    }
    emitter_ptr->owed += emitter_ptr->rate * dt;
    due = (int)emitter_ptr->owed;
    emitter_ptr->owed -= (float)due;
    if (due > emitter_ptr->max) due = emitter_ptr->max;
    /* spread over the frame, in time and along the way the emitter moved, so a steady
       stream doesn't come in clumps */
    for (int k = 0; k < due; k++) {
        const float along = (float)(k + 1) / (float)due;
        spawn(emitter_ptr, emitter_ptr->time - dt * (1.0f - along), sk_v3_add(from, sk_v3_scale(moved, along)));
    }
}

void sk_emitter_update(float dt)
{
    for (uint16_t i = 1; i < sk_emitter3d_pool.capacity; i++) {
        if (sk_emitter3d_pool.occupied[i]) update_emitter(&sk_emitters3d[i], dt);
    }
    for (uint16_t i = 1; i < sk_emitter2d_pool.capacity; i++) {
        if (sk_emitter2d_pool.occupied[i]) update_emitter(&sk_emitters2d[i], dt);
    }
}

/* ------------------------------------------------------------ drawing ---- */

static bool reserve(void **items, int *capacity, int wanted, size_t item_size, int initial)
{
    int grown = *capacity > 0 ? *capacity : initial;
    void *moved;
    if (wanted <= *capacity) return true;
    while (grown < wanted) grown *= 2;
    moved = realloc(*items, item_size * (size_t)grown);
    if (moved == NULL) return false;
    *items = moved;
    *capacity = grown;
    return true;
}

static void draw_callback(int index)
{
    const sk_particle_draw_t *d = &sk_px.draws[index];
    sg_apply_pipeline(sk_px.pipelines[d->pipeline]);
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers = {sk_px.quad, sk_px.buffer},
        .vertex_buffer_offsets[1] = (int)(sizeof(sk_particle_t) * (size_t)d->first),
        .views[VIEW_sprite_tex] = {.id = d->view},
        .samplers[SMP_sprite_smp] = {.id = d->sampler},
    });
    sg_apply_uniforms(UB_sprite_particle_params, &SG_RANGE(d->params));
    /* the clip in effect when it was drawn (it stays for the sokol_gl layer after) */
    sg_apply_scissor_rectf(d->scissor[0], d->scissor[1], d->scissor[2], d->scissor[3], true);
    sg_draw(0, 6, d->count);
}

static void set4(float out[4], float a, float b, float c, float d)
{
    out[0] = a, out[1] = b, out[2] = c, out[3] = d;
}

/* Record an emitter's particles for this frame's pass: its live window into the frame's
 * buffer, and one draw (blended and added particles never write depth). */
static void draw_emitter(sk_handle_t handle)
{
    sk_emitter_t *emitter_ptr = resolve(handle);
    sk_particle_draw_t *d;
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0, first, at;
    float cx, cy, cw, ch;
    const float scale = sk_render_pixel_scale();

    if (!sk_px.ready || emitter_ptr == NULL || !emitter_ptr->visible || emitter_ptr->live == 0 ||
        !sk_texture_get_binding(emitter_ptr->texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    if (!reserve((void **)&sk_px.particles, &sk_px.particle_capacity, sk_px.particle_count + emitter_ptr->live,
                 sizeof(sk_particle_t), PARTICLES_INITIAL) ||
        !reserve((void **)&sk_px.draws, &sk_px.draw_capacity, sk_px.draw_count + 1, sizeof(sk_particle_draw_t),
                 DRAWS_INITIAL)) {
        return;
    }
    d = &sk_px.draws[sk_px.draw_count];
    *d = (sk_particle_draw_t){.view = view.id, .sampler = smp.id, .first = sk_px.particle_count,
                              .count = emitter_ptr->live};

    /* the camera: 3D the active one, facing it; 2D the target's pixels */
    if (emitter_ptr->two_d) {
        const vec2_t size = sk_render_current_pass() == 0 ? sk_window_get_screen_size() : sk_render_target_size();
        const sk_mat4_t ortho = sk_mat4_ortho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f);
        memcpy(d->params.view_proj, ortho.m, sizeof(d->params.view_proj));
        set4(d->params.axis_x, 1, 0, 0, 0);
        set4(d->params.axis_y, 0, -1, 0, 0);
    } else {
        sk_camera3d_t cam;
        const vec2_t size = sk_render_target_size();
        vec3_t right, up;
        if (!sk_camera3d_get_active_data(&cam)) return;
        const sk_mat4_t view_proj = sk_mat4_mul(sk_camera3d_projection(&cam, size.y > 0 ? size.x / size.y : 1.0f),
                                                sk_camera3d_view(&cam));
        memcpy(d->params.view_proj, view_proj.m, sizeof(d->params.view_proj));
        sk_sprite3d_facing_basis(SK_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
        set4(d->params.axis_x, right.x, right.y, right.z, 0);
        set4(d->params.axis_y, up.x, up.y, up.z, 0);
    }
    set4(d->params.gravity_now, emitter_ptr->gravity.x, emitter_ptr->gravity.y, emitter_ptr->gravity.z,
         emitter_ptr->time);
    set4(d->params.dynamics, emitter_ptr->drag, emitter_ptr->stretch, 0, 0);
    set4(d->params.size_mode, emitter_ptr->size_start, emitter_ptr->size_end,
         emitter_ptr->alpha_mode == SK_ALPHA_MASK     ? fmaxf(emitter_ptr->alpha_cutoff, 1e-6f)
         : emitter_ptr->alpha_mode == SK_ALPHA_OPAQUE ? -1.0f
                                                      : 0.0f,
         0);
    set4(d->params.color_start, (float)sk_color_get_red(emitter_ptr->color_start) / 255.0f,
         (float)sk_color_get_green(emitter_ptr->color_start) / 255.0f,
         (float)sk_color_get_blue(emitter_ptr->color_start) / 255.0f,
         (float)sk_color_get_alpha(emitter_ptr->color_start) / 255.0f);
    set4(d->params.color_end, (float)sk_color_get_red(emitter_ptr->color_end) / 255.0f,
         (float)sk_color_get_green(emitter_ptr->color_end) / 255.0f,
         (float)sk_color_get_blue(emitter_ptr->color_end) / 255.0f,
         (float)sk_color_get_alpha(emitter_ptr->color_end) / 255.0f);
    if (emitter_ptr->source[2] > 0.0f && emitter_ptr->source[3] > 0.0f) {
        set4(d->params.source, emitter_ptr->source[0] / (float)tw, emitter_ptr->source[1] / (float)th,
             (emitter_ptr->source[0] + emitter_ptr->source[2]) / (float)tw,
             (emitter_ptr->source[1] + emitter_ptr->source[3]) / (float)th);
    } else {
        set4(d->params.source, 0, 0, 1, 1);
    }
    if (sk_texture_is_flipped(emitter_ptr->texture)) { /* render target stored bottom-up */
        d->params.source[1] = 1.0f - d->params.source[1];
        d->params.source[3] = 1.0f - d->params.source[3];
    }
    d->pipeline = (emitter_ptr->two_d ? PIPELINE_2D_OPAQUE : PIPELINE_3D_OPAQUE) +
                  (emitter_ptr->alpha_mode == SK_ALPHA_ADD     ? 2
                   : emitter_ptr->alpha_mode == SK_ALPHA_BLEND ? 1
                                                               : 0);
    sk_render_get_clip(&cx, &cy, &cw, &ch); /* the whole target when nothing is pushed */
    set4(d->scissor, cx * scale, cy * scale, cw * scale, ch * scale);

    /* the particles that may be alive, oldest first */
    first = sk_px.particle_count;
    at = oldest(emitter_ptr);
    for (int left = emitter_ptr->live; left > 0;) {
        const int run = at + left <= emitter_ptr->max ? left : emitter_ptr->max - at;
        memcpy(&sk_px.particles[first], &emitter_ptr->ring[at], sizeof(sk_particle_t) * (size_t)run);
        first += run;
        left -= run;
        at = 0;
    }
    sk_px.particle_count = first;
    sk_render_submit_callback(draw_callback, sk_px.draw_count++);
}

void sk_emitter_flush(void)
{
    const size_t bytes = sizeof(sk_particle_t) * (size_t)sk_px.particle_count;
    if (!sk_px.ready || sk_px.particle_count == 0) {
        return;
    }
    if (sk_px.particle_count > sk_px.buffer_capacity) {
        int capacity = sk_px.buffer_capacity > 0 ? sk_px.buffer_capacity : PARTICLES_INITIAL;
        while (capacity < sk_px.particle_count) capacity *= 2;
        sg_destroy_buffer(sk_px.buffer);
        sk_px.buffer = sg_make_buffer(&(sg_buffer_desc){
            .size = sizeof(sk_particle_t) * (size_t)capacity,
            .usage = {.vertex_buffer = true, .write_transient = true},
            .label = "sk-particles",
        });
        sk_px.buffer_capacity = capacity;
    }
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .src.data = {.ptr = sk_px.particles, .size = bytes},
        .dst.buffer = sk_px.buffer,
    });
}

void sk_emitter_end_frame(void)
{
    sk_px.particle_count = 0;
    sk_px.draw_count = 0;
}

/* ------------------------------------------------------------ scenes ---- */

static bool in_mode(sk_handle_t handle, bool opaque, bool blend, bool add)
{
    const sk_emitter_t *emitter_ptr = resolve(handle);
    if (emitter_ptr == NULL || !emitter_ptr->visible || emitter_ptr->live == 0) return false;
    switch (emitter_ptr->alpha_mode) {
        case SK_ALPHA_OPAQUE:
        case SK_ALPHA_MASK: return opaque;
        case SK_ALPHA_BLEND: return blend;
        default: return add;
    }
}

static void draw_opaque(sk_handle_t handle)
{
    if (in_mode(handle, true, false, false)) draw_emitter(handle);
}

static void draw_additive(sk_handle_t handle)
{
    if (in_mode(handle, false, false, true)) draw_emitter(handle);
}

/* Blended: sorted with the other transparent parts, as a whole, by its position. */
static int collect_transparent(sk_handle_t handle, const sk_camera3d_t *cam, sk_transparent_item_t *out,
                               int max_items)
{
    const sk_emitter_t *emitter_ptr = resolve(handle);
    if (max_items < 1 || !in_mode(handle, false, true, false)) return 0;
    out[0] = (sk_transparent_item_t){.handle = handle, .depth = sk_scene_view_depth(cam, emitter_ptr->position)};
    return 1;
}

static void draw_transparent(sk_handle_t handle, int part)
{
    (void)part;
    draw_emitter(handle);
}

static bool pick_2d(sk_handle_t handle, float x, float y, sk_pick_result_t *out)
{
    (void)handle, (void)x, (void)y, (void)out;
    return false; /* particles aren't picked */
}

/* ------------------------------------------------------------ lifecycle ---- */

void sk_emitter_init(void)
{
    static const float corners[12] = {-0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, -0.5f};
    const sg_backend backend = sg_query_backend();
    sg_pipeline_desc desc = {
        .layout = {
            .buffers[1] = {.step_func = SG_VERTEXSTEP_PER_INSTANCE},
            .attrs = {
                [ATTR_sprite_particle_corner] = {.format = SG_VERTEXFORMAT_FLOAT2, .buffer_index = 0},
                [ATTR_sprite_particle_born] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_particle_motion] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
                [ATTR_sprite_particle_shape] = {.format = SG_VERTEXFORMAT_FLOAT4, .buffer_index = 1},
            },
        },
        .label = "sk-particles",
    };
    const sg_blend_state blend = {
        .enabled = true,
        .src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA,
        .dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
        .src_factor_alpha = SG_BLENDFACTOR_ONE,
        .dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
    };
    sg_blend_state add = blend;
    add.dst_factor_rgb = SG_BLENDFACTOR_ONE;
    add.dst_factor_alpha = SG_BLENDFACTOR_ONE;

    memset(&sk_px, 0, sizeof(sk_px));
    sk_px.next_seed = 0x9e3779b9u;
    sk_px.shader =
        sg_make_shader(sprite_particle_shader_desc(backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend));
    desc.shader = sk_px.shader;
    desc.depth = (sg_depth_state){.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true};
    sk_px.pipelines[PIPELINE_3D_OPAQUE] = sg_make_pipeline(&desc);
    desc.depth.write_enabled = false;
    desc.colors[0].blend = blend;
    sk_px.pipelines[PIPELINE_3D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    sk_px.pipelines[PIPELINE_3D_ADD] = sg_make_pipeline(&desc);
    desc.depth = (sg_depth_state){.compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false};
    desc.colors[0].blend = (sg_blend_state){0};
    sk_px.pipelines[PIPELINE_2D_OPAQUE] = sg_make_pipeline(&desc);
    desc.colors[0].blend = blend;
    sk_px.pipelines[PIPELINE_2D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    sk_px.pipelines[PIPELINE_2D_ADD] = sg_make_pipeline(&desc);
    sk_px.quad = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(corners), .label = "sk-particle-quad"});
    sk_px.ready = true;

    if (!sk_handle_pool_init(&sk_emitter3d_pool, SK_HANDLE_KIND_EMITTER3D, "emitter3d", (void **)&sk_emitters3d,
                             sizeof(sk_emitter_t), EMITTERS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS) ||
        !sk_handle_pool_init(&sk_emitter2d_pool, SK_HANDLE_KIND_EMITTER2D, "emitter2d", (void **)&sk_emitters2d,
                             sizeof(sk_emitter_t), EMITTERS_INITIAL, SK_HANDLE_POOL_MAX_SLOTS)) {
        log_error("emitters: out of memory");
    }
    sk_scene_register_passes(SK_HANDLE_KIND_EMITTER3D, draw_opaque, collect_transparent, draw_transparent);
    sk_scene_register_additive(SK_HANDLE_KIND_EMITTER3D, draw_additive);
    sk_scene_register_2d(SK_HANDLE_KIND_EMITTER2D, draw_emitter, pick_2d);
}

static void free_emitters(sk_handle_pool_t *pool, sk_emitter_t *items)
{
    for (uint16_t i = 1; i < pool->capacity; i++) {
        if (pool->occupied[i]) {
            free(items[i].ring);
            sk_texture_release(items[i].texture);
        }
    }
}

void sk_emitter_deinit(void)
{
    if (!sk_px.ready) return;
    free_emitters(&sk_emitter3d_pool, sk_emitters3d);
    free_emitters(&sk_emitter2d_pool, sk_emitters2d);
    sk_handle_pool_destroy(&sk_emitter3d_pool);
    sk_handle_pool_destroy(&sk_emitter2d_pool);
    sg_destroy_buffer(sk_px.buffer);
    sg_destroy_buffer(sk_px.quad);
    for (int i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(sk_px.pipelines[i]);
    sg_destroy_shader(sk_px.shader);
    free(sk_px.particles);
    free(sk_px.draws);
    memset(&sk_px, 0, sizeof(sk_px));
}

static sk_handle_t create_emitter(sk_handle_t texture, bool two_d)
{
    sk_handle_pool_t *pool = two_d ? &sk_emitter2d_pool : &sk_emitter3d_pool;
    const sk_handle_t handle = sk_handle_pool_alloc(pool);
    uint16_t index = 0;
    sk_emitter_t *emitter_ptr;

    if (handle == 0) {
        log_error("%s: pool full", two_d ? "emitter2d" : "emitter3d");
        return 0;
    }
    sk_handle_pool_resolve(pool, handle, &index);
    emitter_ptr = two_d ? &sk_emitters2d[index] : &sk_emitters3d[index];
    *emitter_ptr = (sk_emitter_t){
        .two_d = two_d,
        .visible = true,
        .emitting = true,
        .texture = texture,
        .max = DEFAULT_MAX,
        .ring = calloc(DEFAULT_MAX, sizeof(sk_particle_t)),
        .life_min = 1.0f,
        .life_max = 1.0f,
        .size_start = two_d ? 8.0f : 1.0f,
        .size_end = two_d ? 8.0f : 1.0f,
        .color_start = SK_COLOR_WHITE,
        .color_end = SK_COLOR_WHITE,
        .alpha_mode = SK_ALPHA_ADD,
        .alpha_cutoff = 0.5f,
        .rng = sk_px.next_seed,
    };
    sk_px.next_seed = sk_px.next_seed * 1664525u + 1013904223u;
    if (emitter_ptr->rng == 0) emitter_ptr->rng = 1;
    if (emitter_ptr->ring == NULL) {
        sk_handle_pool_free(pool, handle);
        log_error("emitters: out of memory");
        return 0;
    }
    if (texture != 0) sk_texture_retain(texture);
    return handle;
}

static void destroy_emitter(sk_handle_t handle, bool two_d)
{
    sk_emitter_t *emitter_ptr = resolve_kind(handle, two_d);
    if (emitter_ptr == NULL) return;
    sk_scene_forget(handle);
    free(emitter_ptr->ring);
    sk_texture_release(emitter_ptr->texture); /* no-op for 0 */
    *emitter_ptr = (sk_emitter_t){0};
    sk_handle_pool_free(two_d ? &sk_emitter2d_pool : &sk_emitter3d_pool, handle);
}

static bool set_max(sk_emitter_t *emitter_ptr, int count)
{
    sk_particle_t *ring;
    if (emitter_ptr == NULL || count < 1 || count > MAX_PARTICLES) return false;
    ring = calloc((size_t)count, sizeof(sk_particle_t));
    if (ring == NULL) return false;
    free(emitter_ptr->ring);
    emitter_ptr->ring = ring;
    emitter_ptr->max = count;
    emitter_ptr->next = 0;
    emitter_ptr->live = 0; /* the particles alive are gone */
    return true;
}

static int count_alive(const sk_emitter_t *emitter_ptr)
{
    int alive = 0;
    if (emitter_ptr == NULL) return 0;
    for (int i = 0, at = oldest(emitter_ptr); i < emitter_ptr->live; i++, at = (at + 1) % emitter_ptr->max) {
        const float age = emitter_ptr->time - emitter_ptr->ring[at].born[3];
        alive += age >= 0.0f && age < emitter_ptr->ring[at].motion[3] ? 1 : 0;
    }
    return alive;
}

bool sk_emitter_particle(sk_handle_t emitter, int index, float born[4], float motion[4])
{
    const sk_emitter_t *emitter_ptr = resolve(emitter);
    if (emitter_ptr == NULL || index < 0 || index >= emitter_ptr->live) return false;
    const sk_particle_t *p = &emitter_ptr->ring[(oldest(emitter_ptr) + index) % emitter_ptr->max];
    memcpy(born, p->born, sizeof(p->born));
    memcpy(motion, p->motion, sizeof(p->motion));
    return true;
}

/* ------------------------------------------------------------ public API ---- */

/* The setters are the same for both kinds; each kind's API refuses the other's. */
#define WITH_EMITTER(handle, two_d, statement)                                  \
    do {                                                                        \
        sk_emitter_t *emitter_ptr = resolve_kind((handle), (two_d));            \
        if (emitter_ptr == NULL) return false;                                  \
        statement;                                                              \
        return true;                                                            \
    } while (0)

static bool set_life(sk_emitter_t *emitter_ptr, float min_s, float max_s)
{
    if (!(min_s > 0.0f) || max_s < min_s) return false;
    emitter_ptr->life_min = min_s;
    emitter_ptr->life_max = max_s;
    return true;
}

static bool set_alpha_mode(sk_emitter_t *emitter_ptr, sk_alpha_mode_t mode, float cutoff)
{
    if (mode < SK_ALPHA_OPAQUE || mode > SK_ALPHA_ADD) return false;
    emitter_ptr->alpha_mode = mode;
    emitter_ptr->alpha_cutoff = fminf(fmaxf(cutoff, 0.0f), 1.0f);
    return true;
}

/* A move is spread over the next update (steady spawns along the way, velocity to
   inherit); a jump, or the first position, isn't a move. */
static void move_to(sk_emitter_t *emitter_ptr, vec3_t position, bool jump)
{
    emitter_ptr->position = position;
    if (jump || !emitter_ptr->placed) {
        emitter_ptr->previous = position;
        emitter_ptr->movement_velocity = (vec3_t){0, 0, 0};
    }
    emitter_ptr->placed = true;
}

static bool burst(sk_emitter_t *emitter_ptr, int count)
{
    if (count < 0) return false;
    if (count > emitter_ptr->max) count = emitter_ptr->max;
    for (int i = 0; i < count; i++) spawn(emitter_ptr, emitter_ptr->time, emitter_ptr->position);
    return true;
}

SK_KEEP sk_handle_t sk_emitter3d_create(sk_handle_t texture) { return create_emitter(texture, false); }
SK_KEEP sk_handle_t sk_emitter2d_create(sk_handle_t texture) { return create_emitter(texture, true); }
SK_KEEP void sk_emitter3d_destroy(sk_handle_t emitter) { destroy_emitter(emitter, false); }
SK_KEEP void sk_emitter2d_destroy(sk_handle_t emitter) { destroy_emitter(emitter, true); }

SK_KEEP bool sk_emitter3d_set_source(sk_handle_t e, float x, float y, float w, float h)
{
    WITH_EMITTER(e, false, (emitter_ptr->source[0] = x, emitter_ptr->source[1] = y, emitter_ptr->source[2] = w,
                            emitter_ptr->source[3] = h));
}
SK_KEEP bool sk_emitter2d_set_source(sk_handle_t e, float x, float y, float w, float h)
{
    WITH_EMITTER(e, true, (emitter_ptr->source[0] = x, emitter_ptr->source[1] = y, emitter_ptr->source[2] = w,
                           emitter_ptr->source[3] = h));
}
SK_KEEP bool sk_emitter3d_set_position(sk_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, move_to(emitter_ptr, (vec3_t){x, y, z}, false));
}
SK_KEEP bool sk_emitter2d_set_position(sk_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, move_to(emitter_ptr, (vec3_t){x, y, 0.0f}, false));
}
SK_KEEP bool sk_emitter3d_jump(sk_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, move_to(emitter_ptr, (vec3_t){x, y, z}, true));
}
SK_KEEP bool sk_emitter2d_jump(sk_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, move_to(emitter_ptr, (vec3_t){x, y, 0.0f}, true));
}
SK_KEEP vec3_t sk_emitter3d_get_position(sk_handle_t e)
{
    const sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL ? emitter_ptr->position : (vec3_t){0, 0, 0};
}
SK_KEEP vec2_t sk_emitter2d_get_position(sk_handle_t e)
{
    const sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL ? (vec2_t){emitter_ptr->position.x, emitter_ptr->position.y} : (vec2_t){0, 0};
}
SK_KEEP bool sk_emitter3d_set_rate(sk_handle_t e, float per_second)
{
    if (per_second < 0.0f) return false;
    WITH_EMITTER(e, false, emitter_ptr->rate = per_second);
}
SK_KEEP bool sk_emitter2d_set_rate(sk_handle_t e, float per_second)
{
    if (per_second < 0.0f) return false;
    WITH_EMITTER(e, true, emitter_ptr->rate = per_second);
}
SK_KEEP bool sk_emitter3d_burst(sk_handle_t e, int count)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && burst(emitter_ptr, count);
}
SK_KEEP bool sk_emitter2d_burst(sk_handle_t e, int count)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && burst(emitter_ptr, count);
}
SK_KEEP bool sk_emitter3d_set_emitting(sk_handle_t e, bool emitting)
{
    WITH_EMITTER(e, false, emitter_ptr->emitting = emitting);
}
SK_KEEP bool sk_emitter2d_set_emitting(sk_handle_t e, bool emitting)
{
    WITH_EMITTER(e, true, emitter_ptr->emitting = emitting);
}
SK_KEEP bool sk_emitter3d_is_emitting(sk_handle_t e)
{
    const sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && emitter_ptr->emitting;
}
SK_KEEP bool sk_emitter2d_is_emitting(sk_handle_t e)
{
    const sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && emitter_ptr->emitting;
}
SK_KEEP bool sk_emitter3d_set_max(sk_handle_t e, int count) { return set_max(resolve_kind(e, false), count); }
SK_KEEP bool sk_emitter2d_set_max(sk_handle_t e, int count) { return set_max(resolve_kind(e, true), count); }
SK_KEEP bool sk_emitter3d_set_life(sk_handle_t e, float min_s, float max_s)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_life(emitter_ptr, min_s, max_s);
}
SK_KEEP bool sk_emitter2d_set_life(sk_handle_t e, float min_s, float max_s)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_life(emitter_ptr, min_s, max_s);
}
SK_KEEP bool sk_emitter3d_set_spawn_box(sk_handle_t e, float hx, float hy, float hz)
{
    WITH_EMITTER(e, false, emitter_ptr->box = ((vec3_t){fabsf(hx), fabsf(hy), fabsf(hz)}));
}
SK_KEEP bool sk_emitter2d_set_spawn_box(sk_handle_t e, float hw, float hh)
{
    WITH_EMITTER(e, true, emitter_ptr->box = ((vec3_t){fabsf(hw), fabsf(hh), 0.0f}));
}
SK_KEEP bool sk_emitter3d_set_velocity(sk_handle_t e, float x, float y, float z, float spread, float speed_variance)
{
    WITH_EMITTER(e, false, (emitter_ptr->velocity = (vec3_t){x, y, z}, emitter_ptr->spread = fmaxf(spread, 0.0f),
                            emitter_ptr->speed_variance = fminf(fmaxf(speed_variance, 0.0f), 1.0f)));
}
SK_KEEP bool sk_emitter2d_set_velocity(sk_handle_t e, float x, float y, float spread, float speed_variance)
{
    WITH_EMITTER(e, true, (emitter_ptr->velocity = (vec3_t){x, y, 0.0f}, emitter_ptr->spread = fmaxf(spread, 0.0f),
                           emitter_ptr->speed_variance = fminf(fmaxf(speed_variance, 0.0f), 1.0f)));
}
SK_KEEP bool sk_emitter3d_set_gravity(sk_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, emitter_ptr->gravity = ((vec3_t){x, y, z}));
}
SK_KEEP bool sk_emitter2d_set_gravity(sk_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, emitter_ptr->gravity = ((vec3_t){x, y, 0.0f}));
}
SK_KEEP bool sk_emitter3d_set_drag(sk_handle_t e, float per_second)
{
    if (!(per_second >= 0.0f)) return false;
    WITH_EMITTER(e, false, emitter_ptr->drag = per_second);
}
SK_KEEP bool sk_emitter2d_set_drag(sk_handle_t e, float per_second)
{
    if (!(per_second >= 0.0f)) return false;
    WITH_EMITTER(e, true, emitter_ptr->drag = per_second);
}
SK_KEEP bool sk_emitter3d_set_stretch(sk_handle_t e, float seconds)
{
    if (!(seconds >= 0.0f)) return false;
    WITH_EMITTER(e, false, emitter_ptr->stretch = seconds);
}
SK_KEEP bool sk_emitter2d_set_stretch(sk_handle_t e, float seconds)
{
    if (!(seconds >= 0.0f)) return false;
    WITH_EMITTER(e, true, emitter_ptr->stretch = seconds);
}
SK_KEEP bool sk_emitter3d_set_inherit_velocity(sk_handle_t e, float fraction)
{
    WITH_EMITTER(e, false, emitter_ptr->inherit = fraction);
}
SK_KEEP bool sk_emitter2d_set_inherit_velocity(sk_handle_t e, float fraction)
{
    WITH_EMITTER(e, true, emitter_ptr->inherit = fraction);
}
SK_KEEP bool sk_emitter3d_set_size(sk_handle_t e, float start, float end, float variance)
{
    WITH_EMITTER(e, false, (emitter_ptr->size_start = fmaxf(start, 0.0f), emitter_ptr->size_end = fmaxf(end, 0.0f),
                            emitter_ptr->size_variance = fminf(fmaxf(variance, 0.0f), 1.0f)));
}
SK_KEEP bool sk_emitter2d_set_size(sk_handle_t e, float start, float end, float variance)
{
    WITH_EMITTER(e, true, (emitter_ptr->size_start = fmaxf(start, 0.0f), emitter_ptr->size_end = fmaxf(end, 0.0f),
                           emitter_ptr->size_variance = fminf(fmaxf(variance, 0.0f), 1.0f)));
}
SK_KEEP bool sk_emitter3d_set_color(sk_handle_t e, sk_color_t start, sk_color_t end)
{
    WITH_EMITTER(e, false, (emitter_ptr->color_start = start, emitter_ptr->color_end = end));
}
SK_KEEP bool sk_emitter2d_set_color(sk_handle_t e, sk_color_t start, sk_color_t end)
{
    WITH_EMITTER(e, true, (emitter_ptr->color_start = start, emitter_ptr->color_end = end));
}
SK_KEEP bool sk_emitter3d_set_spin(sk_handle_t e, float min, float max)
{
    WITH_EMITTER(e, false, (emitter_ptr->spin_min = min, emitter_ptr->spin_max = max));
}
SK_KEEP bool sk_emitter2d_set_spin(sk_handle_t e, float min, float max)
{
    WITH_EMITTER(e, true, (emitter_ptr->spin_min = min, emitter_ptr->spin_max = max));
}
SK_KEEP bool sk_emitter3d_set_alpha_mode(sk_handle_t e, sk_alpha_mode_t mode, float cutoff)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_alpha_mode(emitter_ptr, mode, cutoff);
}
SK_KEEP bool sk_emitter2d_set_alpha_mode(sk_handle_t e, sk_alpha_mode_t mode, float cutoff)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_alpha_mode(emitter_ptr, mode, cutoff);
}
SK_KEEP bool sk_emitter3d_set_seed(sk_handle_t e, unsigned int seed)
{
    WITH_EMITTER(e, false, emitter_ptr->rng = seed != 0 ? seed : 1u);
}
SK_KEEP bool sk_emitter2d_set_seed(sk_handle_t e, unsigned int seed)
{
    WITH_EMITTER(e, true, emitter_ptr->rng = seed != 0 ? seed : 1u);
}
SK_KEEP int sk_emitter3d_get_count(sk_handle_t e) { return count_alive(resolve_kind(e, false)); }
SK_KEEP int sk_emitter2d_get_count(sk_handle_t e) { return count_alive(resolve_kind(e, true)); }
SK_KEEP void sk_emitter3d_clear(sk_handle_t e)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, false);
    if (emitter_ptr != NULL) emitter_ptr->live = 0;
}
SK_KEEP void sk_emitter2d_clear(sk_handle_t e)
{
    sk_emitter_t *emitter_ptr = resolve_kind(e, true);
    if (emitter_ptr != NULL) emitter_ptr->live = 0;
}
SK_KEEP bool sk_emitter3d_set_visible(sk_handle_t e, bool visible)
{
    WITH_EMITTER(e, false, emitter_ptr->visible = visible);
}
SK_KEEP bool sk_emitter2d_set_visible(sk_handle_t e, bool visible)
{
    WITH_EMITTER(e, true, emitter_ptr->visible = visible);
}
SK_KEEP void sk_emitter3d_draw(sk_handle_t e)
{
    if (resolve_kind(e, false) != NULL) draw_emitter(e);
}
SK_KEEP void sk_emitter2d_draw(sk_handle_t e)
{
    if (resolve_kind(e, true) != NULL) draw_emitter(e);
}
