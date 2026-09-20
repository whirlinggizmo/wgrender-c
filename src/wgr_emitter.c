#include "wgr_emitter2d.h"
#include "wgr_emitter3d.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_color_internal.h"
#include "internal/wgr_emitter_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_math_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_shaders_internal.h"
#include "internal/wgr_sprite3d_internal.h"
#include "internal/wgr_texture_internal.h"
#include "internal/wgr_module_internal.h"
#include "wgr_color.h"
#include "wgr_logger.h"
#include "wgr_window.h"

/* Particle emitters (docs/PLAN-sprites.md, step 4). A particle is a birth record:
 * where and when it was born, how fast it went, how long it lives, its size scale,
 * spin and starting angle. The CPU decides those at birth (within the emitter's
 * ranges) and never touches the particle again; the shader (vs_particle in
 * wgr_sprite.glsl) works out where it is, how big, what color and how turned from its
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
#define MAX_KEYS 8    /* per curve */
#define MAX_PALETTE 8

/* One particle as the shader reads it (per-instance data, 48 bytes). */
typedef struct {
    float born[4];   /* xyz where, w when (the emitter's clock) */
    float motion[4]; /* xyz velocity, w life (seconds) */
    float shape[4];  /* x size scale, y spin (radians / s), z angle at birth, w a random 0..1 */
} wgr_particle_t;

typedef struct {
    bool two_d;
    bool visible;
    bool emitting;
    wgr_handle_t texture;
    float source[4]; /* texture pixels; width or height <= 0: the whole texture */
    vec3_t position;
    vec3_t previous;    /* where it was at the last update: steady spawns spread along the way */
    vec3_t movement_velocity; /* how fast it moved over the last update */
    float last_dt;      /* the frame time the game moved it by since then */
    bool placed;        /* positioned once (the first position doesn't count as a move) */
    float rate;      /* per second */
    float owed;      /* particles due but not yet spawned (fractions carry over) */
    int max;
    wgr_particle_t *ring;
    int next;        /* where the next particle goes */
    int live;        /* particles in the window: the `live` before `next` may be alive */
    float life_min, life_max;
    vec3_t box;      /* spawn box half sizes */
    float sphere;    /* > 0: spawn in a sphere (2D: a circle) of this radius instead */
    vec3_t velocity; /* direction x speed */
    float spread, speed_variance;
    vec3_t gravity;
    float drag;         /* per second */
    float stretch;      /* seconds of motion */
    float inherit;      /* of the emitter's own velocity */
    float size_times[MAX_KEYS], size_values[MAX_KEYS]; /* over life, keys in order */
    int size_keys;
    float size_variance;
    float color_times[MAX_KEYS];
    wgr_color_t color_values[MAX_KEYS];
    int color_keys;
    wgr_color_t palette[MAX_PALETTE];
    int palette_count;
    float spin_min, spin_max;
    int columns, rows, frames; /* flipbook: frames < 2 none */
    float frames_per_second;   /* 0: once over the life */
    wgr_alpha_mode_t alpha_mode;
    float alpha_cutoff;
    uint32_t rng;
    float time;      /* the emitter's clock, seconds (rebased now and then) */
} wgr_emitter_t;

/* One emitter's draw this frame. */
typedef struct {
    int pipeline;
    uint32_t view, sampler;
    int first, count; /* its particles in the frame's buffer */
    float scissor[4];
    sprite_particle_params_t params;
} wgr_particle_draw_t;

enum {
    PIPELINE_3D_OPAQUE, /* opaque and masked: depth written */
    PIPELINE_3D_BLEND,  /* blended and added: depth tested, not written */
    PIPELINE_3D_ADD,
    PIPELINE_2D_OPAQUE, /* 2D: no depth */
    PIPELINE_2D_BLEND,
    PIPELINE_2D_ADD,
    PIPELINE_COUNT
};

static wgr_emitter_t *wgr_emitters3d;
static wgr_emitter_t *wgr_emitters2d;
static wgri_handle_pool_t wgr_emitter3d_pool;
static wgri_handle_pool_t wgr_emitter2d_pool;

static struct {
    bool ready;
    sg_shader shader;
    sg_pipeline pipelines[PIPELINE_COUNT];
    sg_buffer quad;
    sg_buffer buffer;
    int buffer_capacity;
    wgr_particle_t *particles; /* the frame's, for all emitters */
    int particle_count, particle_capacity;
    wgr_particle_draw_t *draws;
    int draw_count, draw_capacity;
    uint32_t next_seed;
} wgr_px;

/* ------------------------------------------------------------ handles ---- */

static wgr_emitter_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (wgri_handle_pool_resolve(&wgr_emitter3d_pool, handle, &index)) return &wgr_emitters3d[index];
    if (wgri_handle_pool_resolve(&wgr_emitter2d_pool, handle, &index)) return &wgr_emitters2d[index];
    if (handle != 0) log_warn("Invalid emitter handle (%u)", (unsigned int)handle);
    return NULL;
}

/* An emitter of the kind a function is for (3D functions refuse 2D emitters). */
static wgr_emitter_t *resolve_kind(wgr_handle_t handle, bool two_d)
{
    wgr_emitter_t *emitter_ptr = resolve(handle);
    return emitter_ptr != NULL && emitter_ptr->two_d == two_d ? emitter_ptr : NULL;
}

/* ------------------------------------------------------------ random ---- */

static float random01(wgr_emitter_t *emitter_ptr)
{
    uint32_t x = emitter_ptr->rng; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    emitter_ptr->rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static float random_between(wgr_emitter_t *emitter_ptr, float a, float b)
{
    return a + (b - a) * random01(emitter_ptr);
}

static float signed_random(wgr_emitter_t *emitter_ptr)
{
    return random01(emitter_ptr) * 2.0f - 1.0f;
}

/* ------------------------------------------------------------ spawning ---- */

/* A velocity for a new particle: the emitter's, turned within its spread and scaled
 * within its speed variance. */
static vec3_t birth_velocity(wgr_emitter_t *emitter_ptr)
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
        vec3_t u = wgri_v3_norm(wgri_v3_cross(helper, dir));
        const vec3_t w = wgri_v3_cross(dir, u);
        dir = (vec3_t){dir.x * cos_theta + (u.x * cosf(phi) + w.x * sinf(phi)) * sin_theta,
                       dir.y * cos_theta + (u.y * cosf(phi) + w.y * sinf(phi)) * sin_theta,
                       dir.z * cos_theta + (u.z * cosf(phi) + w.z * sinf(phi)) * sin_theta};
    }
    return (vec3_t){dir.x * speed, dir.y * speed, dir.z * speed};
}

/* Where in the spawn shape, from its middle: anywhere in the box, or evenly within the
 * sphere (circle). */
static vec3_t spawn_offset(wgr_emitter_t *emitter_ptr)
{
    const vec3_t box = emitter_ptr->box;
    if (emitter_ptr->sphere > 0.0f) {
        const float phi = TAU * random01(emitter_ptr);
        if (emitter_ptr->two_d) {
            const float r = emitter_ptr->sphere * sqrtf(random01(emitter_ptr));
            return (vec3_t){cosf(phi) * r, sinf(phi) * r, 0.0f};
        }
        const float z = signed_random(emitter_ptr), ring = sqrtf(fmaxf(0.0f, 1.0f - z * z));
        const float r = emitter_ptr->sphere * cbrtf(random01(emitter_ptr));
        return (vec3_t){cosf(phi) * ring * r, sinf(phi) * ring * r, z * r};
    }
    return (vec3_t){box.x * signed_random(emitter_ptr), box.y * signed_random(emitter_ptr),
                    box.z * signed_random(emitter_ptr)};
}

/* A new particle, born at `when` on the emitter's clock around `at` (replacing the
 * oldest when the ring is full). */
static void spawn(wgr_emitter_t *emitter_ptr, float when, vec3_t at)
{
    const vec3_t v = birth_velocity(emitter_ptr);
    const vec3_t offset = spawn_offset(emitter_ptr);
    const vec3_t moving = wgri_v3_scale(emitter_ptr->movement_velocity, emitter_ptr->inherit);
    wgr_particle_t *p = &emitter_ptr->ring[emitter_ptr->next];

    *p = (wgr_particle_t){
        .born = {at.x + offset.x, at.y + offset.y, emitter_ptr->two_d ? 0.0f : at.z + offset.z, when},
        .motion = {v.x + moving.x, v.y + moving.y, emitter_ptr->two_d ? 0.0f : v.z + moving.z,
                   random_between(emitter_ptr, emitter_ptr->life_min, emitter_ptr->life_max)},
        .shape = {fmaxf(0.0f, 1.0f + emitter_ptr->size_variance * signed_random(emitter_ptr)),
                  random_between(emitter_ptr, emitter_ptr->spin_min, emitter_ptr->spin_max),
                  TAU * random01(emitter_ptr), random01(emitter_ptr)},
    };
    emitter_ptr->next = (emitter_ptr->next + 1) % emitter_ptr->max;
    if (emitter_ptr->live < emitter_ptr->max) emitter_ptr->live++;
}

static int oldest(const wgr_emitter_t *emitter_ptr)
{
    return (emitter_ptr->next - emitter_ptr->live + emitter_ptr->max) % emitter_ptr->max;
}

/* Drop particles from the old end of the window once none of them can be alive. */
static void retire(wgr_emitter_t *emitter_ptr)
{
    while (emitter_ptr->live > 0) {
        const wgr_particle_t *p = &emitter_ptr->ring[oldest(emitter_ptr)];
        if (emitter_ptr->time - p->born[3] < p->motion[3]) break;
        emitter_ptr->live--;
    }
}

static void update_emitter(wgr_emitter_t *emitter_ptr, float dt)
{
    const vec3_t from = emitter_ptr->previous;
    const vec3_t moved = wgri_v3_sub(emitter_ptr->position, from);
    int due;

    emitter_ptr->time += dt;
    if (emitter_ptr->time > REBASE_SECONDS) { /* keep the clock small for the shader's floats */
        for (int i = 0, at = oldest(emitter_ptr); i < emitter_ptr->live; i++, at = (at + 1) % emitter_ptr->max) {
            emitter_ptr->ring[at].born[3] -= REBASE_SECONDS;
        }
        emitter_ptr->time -= REBASE_SECONDS;
    }
    retire(emitter_ptr);
    /* the move was made after the last update, in a frame of that update's length (the
       game moves things by the frame's time): how fast it went is over that, not this */
    emitter_ptr->previous = emitter_ptr->position;
    if (emitter_ptr->last_dt > 0.0f) emitter_ptr->movement_velocity = wgri_v3_scale(moved, 1.0f / emitter_ptr->last_dt);
    emitter_ptr->last_dt = dt;
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
        spawn(emitter_ptr, emitter_ptr->time - dt * (1.0f - along), wgri_v3_add(from, wgri_v3_scale(moved, along)));
    }
}

void wgri_emitter_update(float dt)
{
    for (uint16_t i = 1; i < wgr_emitter3d_pool.capacity; i++) {
        if (wgr_emitter3d_pool.occupied[i]) update_emitter(&wgr_emitters3d[i], dt);
    }
    for (uint16_t i = 1; i < wgr_emitter2d_pool.capacity; i++) {
        if (wgr_emitter2d_pool.occupied[i]) update_emitter(&wgr_emitters2d[i], dt);
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
    const wgr_particle_draw_t *d = &wgr_px.draws[index];
    sg_apply_pipeline(wgr_px.pipelines[d->pipeline]);
    sg_apply_bindings(&(sg_bindings){
        .vertex_buffers = {wgr_px.quad, wgr_px.buffer},
        .vertex_buffer_offsets[1] = (int)(sizeof(wgr_particle_t) * (size_t)d->first),
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

/* A color as the shader takes it: sRGB values 0..1, like sokol_gl's. */
static void color4(float out[4], wgr_color_t color)
{
    set4(out, (float)wgr_color_get_red(color) / 255.0f, (float)wgr_color_get_green(color) / 255.0f,
         (float)wgr_color_get_blue(color) / 255.0f, (float)wgr_color_get_alpha(color) / 255.0f);
}

/* Record an emitter's particles for this frame's pass: its live window into the frame's
 * buffer, and one draw (blended and added particles never write depth). */
static void draw_emitter(wgr_handle_t handle)
{
    wgr_emitter_t *emitter_ptr = resolve(handle);
    wgr_particle_draw_t *d;
    sg_view view;
    sg_sampler smp;
    int tw = 0, th = 0, first, at;
    float cx, cy, cw, ch;
    const float scale = wgri_render_pixel_scale();

    if (!wgr_px.ready || emitter_ptr == NULL || !emitter_ptr->visible || emitter_ptr->live == 0 ||
        emitter_ptr->size_keys == 0 || emitter_ptr->color_keys == 0 ||
        !wgri_texture_get_binding(emitter_ptr->texture, &view, &smp, &tw, &th) || tw <= 0 || th <= 0) {
        return;
    }
    if (!reserve((void **)&wgr_px.particles, &wgr_px.particle_capacity, wgr_px.particle_count + emitter_ptr->live,
                 sizeof(wgr_particle_t), PARTICLES_INITIAL) ||
        !reserve((void **)&wgr_px.draws, &wgr_px.draw_capacity, wgr_px.draw_count + 1, sizeof(wgr_particle_draw_t),
                 DRAWS_INITIAL)) {
        return;
    }
    d = &wgr_px.draws[wgr_px.draw_count];
    *d = (wgr_particle_draw_t){.view = view.id, .sampler = smp.id, .first = wgr_px.particle_count,
                              .count = emitter_ptr->live};

    /* the camera: 3D the active one, facing it; 2D the target's pixels */
    if (emitter_ptr->two_d) {
        const vec2_t size = wgri_render_current_pass() == 0 ? wgr_window_get_screen_size() : wgri_render_target_size();
        const wgri_mat4_t ortho = wgri_mat4_ortho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f);
        memcpy(d->params.view_proj, ortho.m, sizeof(d->params.view_proj));
        set4(d->params.axis_x, 1, 0, 0, 0);
        set4(d->params.axis_y, 0, -1, 0, 0);
    } else {
        wgri_camera3d_t cam;
        const vec2_t size = wgri_render_target_size();
        vec3_t right, up;
        if (!wgri_camera3d_get_active_data(&cam)) return;
        const wgri_mat4_t view_proj = wgri_mat4_mul(wgri_camera3d_projection(&cam, size.y > 0 ? size.x / size.y : 1.0f),
                                                wgri_camera3d_view(&cam));
        memcpy(d->params.view_proj, view_proj.m, sizeof(d->params.view_proj));
        wgri_sprite3d_facing_basis(WGR_SPRITE3D_FACING_CAMERA, (vec3_t){0, 0, 0}, &cam, &right, &up);
        set4(d->params.axis_x, right.x, right.y, right.z, 0);
        set4(d->params.axis_y, up.x, up.y, up.z, 0);
    }
    set4(d->params.gravity_now, emitter_ptr->gravity.x, emitter_ptr->gravity.y, emitter_ptr->gravity.z,
         emitter_ptr->time);
    set4(d->params.dynamics, emitter_ptr->drag, emitter_ptr->stretch, 0, 0);
    set4(d->params.frames, (float)emitter_ptr->columns, (float)emitter_ptr->rows, (float)emitter_ptr->frames,
         emitter_ptr->frames_per_second);
    set4(d->params.counts, (float)emitter_ptr->size_keys, (float)emitter_ptr->color_keys,
         emitter_ptr->alpha_mode == WGR_ALPHA_MASK     ? fmaxf(emitter_ptr->alpha_cutoff, 1e-6f)
         : emitter_ptr->alpha_mode == WGR_ALPHA_OPAQUE ? -1.0f
                                                      : 0.0f,
         (float)emitter_ptr->palette_count);
    for (int i = 0; i < MAX_KEYS; i++) { /* the curves, 4 keys to a vec4 */
        d->params.size_times[i / 4][i % 4] = emitter_ptr->size_times[i];
        d->params.size_values[i / 4][i % 4] = emitter_ptr->size_values[i];
        d->params.color_times[i / 4][i % 4] = emitter_ptr->color_times[i];
        color4(d->params.color_values[i], emitter_ptr->color_values[i]);
    }
    for (int i = 0; i < emitter_ptr->palette_count; i++) color4(d->params.palette[i], emitter_ptr->palette[i]);
    if (emitter_ptr->source[2] > 0.0f && emitter_ptr->source[3] > 0.0f) {
        set4(d->params.source, emitter_ptr->source[0] / (float)tw, emitter_ptr->source[1] / (float)th,
             (emitter_ptr->source[0] + emitter_ptr->source[2]) / (float)tw,
             (emitter_ptr->source[1] + emitter_ptr->source[3]) / (float)th);
    } else {
        set4(d->params.source, 0, 0, 1, 1);
    }
    if (wgri_texture_is_flipped(emitter_ptr->texture)) { /* render target stored bottom-up */
        d->params.source[1] = 1.0f - d->params.source[1];
        d->params.source[3] = 1.0f - d->params.source[3];
    }
    d->pipeline = (emitter_ptr->two_d ? PIPELINE_2D_OPAQUE : PIPELINE_3D_OPAQUE) +
                  (emitter_ptr->alpha_mode == WGR_ALPHA_ADD     ? 2
                   : emitter_ptr->alpha_mode == WGR_ALPHA_BLEND ? 1
                                                               : 0);
    wgri_render_get_clip(&cx, &cy, &cw, &ch); /* the whole target when nothing is pushed */
    set4(d->scissor, cx * scale, cy * scale, cw * scale, ch * scale);

    /* the particles that may be alive, oldest first */
    first = wgr_px.particle_count;
    at = oldest(emitter_ptr);
    for (int left = emitter_ptr->live; left > 0;) {
        const int run = at + left <= emitter_ptr->max ? left : emitter_ptr->max - at;
        memcpy(&wgr_px.particles[first], &emitter_ptr->ring[at], sizeof(wgr_particle_t) * (size_t)run);
        first += run;
        left -= run;
        at = 0;
    }
    wgr_px.particle_count = first;
    wgri_render_submit_callback(draw_callback, wgr_px.draw_count++);
}

void wgri_emitter_flush(void)
{
    const size_t bytes = sizeof(wgr_particle_t) * (size_t)wgr_px.particle_count;
    if (!wgr_px.ready || wgr_px.particle_count == 0) {
        return;
    }
    if (wgr_px.particle_count > wgr_px.buffer_capacity) {
        int capacity = wgr_px.buffer_capacity > 0 ? wgr_px.buffer_capacity : PARTICLES_INITIAL;
        while (capacity < wgr_px.particle_count) capacity *= 2;
        sg_destroy_buffer(wgr_px.buffer);
        wgr_px.buffer = sg_make_buffer(&(sg_buffer_desc){
            .size = sizeof(wgr_particle_t) * (size_t)capacity,
            .usage = {.vertex_buffer = true, .write_transient = true},
            .label = "wgr-particles",
        });
        wgr_px.buffer_capacity = capacity;
    }
    sg_write_buffer_transient(&(sg_write_buffer_desc){
        .src.data = {.ptr = wgr_px.particles, .size = bytes},
        .dst.buffer = wgr_px.buffer,
    });
}

void wgri_emitter_end_frame(void)
{
    wgr_px.particle_count = 0;
    wgr_px.draw_count = 0;
}

/* ------------------------------------------------------------ scenes ---- */

static bool in_mode(wgr_handle_t handle, bool opaque, bool blend, bool add)
{
    const wgr_emitter_t *emitter_ptr = resolve(handle);
    if (emitter_ptr == NULL || !emitter_ptr->visible || emitter_ptr->live == 0) return false;
    switch (emitter_ptr->alpha_mode) {
        case WGR_ALPHA_OPAQUE:
        case WGR_ALPHA_MASK: return opaque;
        case WGR_ALPHA_BLEND: return blend;
        default: return add;
    }
}

static void draw_opaque(wgr_handle_t handle)
{
    if (in_mode(handle, true, false, false)) draw_emitter(handle);
}

static void draw_additive(wgr_handle_t handle)
{
    if (in_mode(handle, false, false, true)) draw_emitter(handle);
}

/* Blended: sorted with the other transparent parts, as a whole, by its position. */
static int collect_transparent(wgr_handle_t handle, const wgri_camera3d_t *cam, wgri_transparent_item_t *out,
                               int max_items)
{
    const wgr_emitter_t *emitter_ptr = resolve(handle);
    if (max_items < 1 || !in_mode(handle, false, true, false)) return 0;
    out[0] = (wgri_transparent_item_t){.handle = handle, .depth = wgri_scene_view_depth(cam, emitter_ptr->position)};
    return 1;
}

static void draw_transparent(wgr_handle_t handle, int part)
{
    (void)part;
    draw_emitter(handle);
}

static bool pick_2d(wgr_handle_t handle, float x, float y, wgr_pick_result_t *out)
{
    (void)handle, (void)x, (void)y, (void)out;
    return false; /* particles aren't picked */
}

/* ------------------------------------------------------------ lifecycle ---- */

/* The shader, pipelines and quad, made with the first emitter rather than at startup:
 * a program without particles doesn't compile them (tools/webstart.mjs). */
static void ensure_gpu(void)
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
        .label = "wgr-particles",
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

    if (wgr_px.ready) return;
    wgr_px.shader =
        sg_make_shader(sprite_particle_shader_desc(backend == SG_BACKEND_DUMMY ? SG_BACKEND_GLCORE : backend));
    desc.shader = wgr_px.shader;
    desc.depth = (sg_depth_state){.compare = SG_COMPAREFUNC_LESS_EQUAL, .write_enabled = true};
    wgr_px.pipelines[PIPELINE_3D_OPAQUE] = sg_make_pipeline(&desc);
    desc.depth.write_enabled = false;
    desc.colors[0].blend = blend;
    wgr_px.pipelines[PIPELINE_3D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    wgr_px.pipelines[PIPELINE_3D_ADD] = sg_make_pipeline(&desc);
    desc.depth = (sg_depth_state){.compare = SG_COMPAREFUNC_ALWAYS, .write_enabled = false};
    desc.colors[0].blend = (sg_blend_state){0};
    wgr_px.pipelines[PIPELINE_2D_OPAQUE] = sg_make_pipeline(&desc);
    desc.colors[0].blend = blend;
    wgr_px.pipelines[PIPELINE_2D_BLEND] = sg_make_pipeline(&desc);
    desc.colors[0].blend = add;
    wgr_px.pipelines[PIPELINE_2D_ADD] = sg_make_pipeline(&desc);
    wgr_px.quad = sg_make_buffer(&(sg_buffer_desc){.data = SG_RANGE(corners), .label = "wgr-particle-quad"});
    wgr_px.ready = true;
}

void wgri_emitter_init(void)
{
    memset(&wgr_px, 0, sizeof(wgr_px));
    wgr_px.next_seed = 0x9e3779b9u;
    if (!wgri_handle_pool_init(&wgr_emitter3d_pool, WGR_HANDLE_KIND_EMITTER3D, "emitter3d", (void **)&wgr_emitters3d,
                             sizeof(wgr_emitter_t), EMITTERS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS) ||
        !wgri_handle_pool_init(&wgr_emitter2d_pool, WGR_HANDLE_KIND_EMITTER2D, "emitter2d", (void **)&wgr_emitters2d,
                             sizeof(wgr_emitter_t), EMITTERS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("emitters: out of memory");
    }
    wgri_scene_register_passes(WGR_HANDLE_KIND_EMITTER3D, draw_opaque, collect_transparent, draw_transparent);
    wgri_scene_register_additive(WGR_HANDLE_KIND_EMITTER3D, draw_additive);
    wgri_scene_register_2d(WGR_HANDLE_KIND_EMITTER2D, draw_emitter, pick_2d);
}

static void free_emitters(wgri_handle_pool_t *pool, wgr_emitter_t *items)
{
    for (uint16_t i = 1; i < pool->capacity; i++) {
        if (pool->occupied[i]) {
            free(items[i].ring);
            wgr_texture_release(items[i].texture);
        }
    }
}

void wgri_emitter_deinit(void)
{
    free_emitters(&wgr_emitter3d_pool, wgr_emitters3d);
    free_emitters(&wgr_emitter2d_pool, wgr_emitters2d);
    wgri_handle_pool_destroy(&wgr_emitter3d_pool);
    wgri_handle_pool_destroy(&wgr_emitter2d_pool);
    if (wgr_px.ready) {
        sg_destroy_buffer(wgr_px.buffer);
        sg_destroy_buffer(wgr_px.quad);
        for (int i = 0; i < PIPELINE_COUNT; i++) sg_destroy_pipeline(wgr_px.pipelines[i]);
        sg_destroy_shader(wgr_px.shader);
    }
    free(wgr_px.particles);
    free(wgr_px.draws);
    memset(&wgr_px, 0, sizeof(wgr_px));
}

static wgr_handle_t create_emitter(wgr_handle_t texture, bool two_d)
{
    wgri_handle_pool_t *pool = two_d ? &wgr_emitter2d_pool : &wgr_emitter3d_pool;
    const wgr_handle_t handle = wgri_handle_pool_alloc(pool);
    uint16_t index = 0;
    wgr_emitter_t *emitter_ptr;

    if (handle == 0) {
        log_error("%s: pool full", two_d ? "emitter2d" : "emitter3d");
        return 0;
    }
    ensure_gpu();
    wgri_handle_pool_resolve(pool, handle, &index);
    emitter_ptr = two_d ? &wgr_emitters2d[index] : &wgr_emitters3d[index];
    *emitter_ptr = (wgr_emitter_t){
        .two_d = two_d,
        .visible = true,
        .emitting = true,
        .texture = texture,
        .max = DEFAULT_MAX,
        .ring = calloc(DEFAULT_MAX, sizeof(wgr_particle_t)),
        .life_min = 1.0f,
        .life_max = 1.0f,
        .size_times = {0.0f, 1.0f},
        .size_values = {two_d ? 8.0f : 1.0f, two_d ? 8.0f : 1.0f},
        .size_keys = 2,
        .color_times = {0.0f, 1.0f},
        .color_values = {WGR_COLOR_WHITE, WGR_COLOR_WHITE},
        .color_keys = 2,
        .columns = 1,
        .rows = 1,
        .frames = 1,
        .alpha_mode = WGR_ALPHA_ADD,
        .alpha_cutoff = 0.5f,
        .rng = wgr_px.next_seed,
    };
    wgr_px.next_seed = wgr_px.next_seed * 1664525u + 1013904223u;
    if (emitter_ptr->rng == 0) emitter_ptr->rng = 1;
    if (emitter_ptr->ring == NULL) {
        wgri_handle_pool_free(pool, handle);
        log_error("emitters: out of memory");
        return 0;
    }
    if (texture != 0) wgri_texture_retain(texture);
    return handle;
}

static void destroy_emitter(wgr_handle_t handle, bool two_d)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(handle, two_d);
    if (emitter_ptr == NULL) return;
    wgri_scene_forget(handle);
    free(emitter_ptr->ring);
    wgr_texture_release(emitter_ptr->texture); /* no-op for 0 */
    *emitter_ptr = (wgr_emitter_t){0};
    wgri_handle_pool_free(two_d ? &wgr_emitter2d_pool : &wgr_emitter3d_pool, handle);
}

static bool set_max(wgr_emitter_t *emitter_ptr, int count)
{
    wgr_particle_t *ring;
    if (emitter_ptr == NULL || count < 1 || count > MAX_PARTICLES) return false;
    ring = calloc((size_t)count, sizeof(wgr_particle_t));
    if (ring == NULL) return false;
    free(emitter_ptr->ring);
    emitter_ptr->ring = ring;
    emitter_ptr->max = count;
    emitter_ptr->next = 0;
    emitter_ptr->live = 0; /* the particles alive are gone */
    return true;
}

static int count_alive(const wgr_emitter_t *emitter_ptr)
{
    int alive = 0;
    if (emitter_ptr == NULL) return 0;
    for (int i = 0, at = oldest(emitter_ptr); i < emitter_ptr->live; i++, at = (at + 1) % emitter_ptr->max) {
        const float age = emitter_ptr->time - emitter_ptr->ring[at].born[3];
        alive += age >= 0.0f && age < emitter_ptr->ring[at].motion[3] ? 1 : 0;
    }
    return alive;
}

bool wgri_emitter_particle(wgr_handle_t emitter, int index, float born[4], float motion[4], float shape[4])
{
    const wgr_emitter_t *emitter_ptr = resolve(emitter);
    if (emitter_ptr == NULL || index < 0 || index >= emitter_ptr->live) return false;
    const wgr_particle_t *p = &emitter_ptr->ring[(oldest(emitter_ptr) + index) % emitter_ptr->max];
    memcpy(born, p->born, sizeof(p->born));
    memcpy(motion, p->motion, sizeof(p->motion));
    memcpy(shape, p->shape, sizeof(p->shape));
    return true;
}

int wgri_emitter_size_keys(wgr_handle_t emitter, float times[8], float values[8])
{
    const wgr_emitter_t *emitter_ptr = resolve(emitter);
    if (emitter_ptr == NULL) return 0;
    memcpy(times, emitter_ptr->size_times, sizeof(emitter_ptr->size_times));
    memcpy(values, emitter_ptr->size_values, sizeof(emitter_ptr->size_values));
    return emitter_ptr->size_keys;
}

/* ------------------------------------------------------------ public API ---- */

/* The setters are the same for both kinds; each kind's API refuses the other's. */
#define WITH_EMITTER(handle, two_d, statement)                                  \
    do {                                                                        \
        wgr_emitter_t *emitter_ptr = resolve_kind((handle), (two_d));            \
        if (emitter_ptr == NULL) return false;                                  \
        statement;                                                              \
        return true;                                                            \
    } while (0)

static bool set_life(wgr_emitter_t *emitter_ptr, float min_s, float max_s)
{
    if (!(min_s > 0.0f) || max_s < min_s) return false;
    emitter_ptr->life_min = min_s;
    emitter_ptr->life_max = max_s;
    return true;
}

static bool set_alpha_mode(wgr_emitter_t *emitter_ptr, wgr_alpha_mode_t mode, float cutoff)
{
    if (mode < WGR_ALPHA_OPAQUE || mode > WGR_ALPHA_ADD) return false;
    emitter_ptr->alpha_mode = mode;
    emitter_ptr->alpha_cutoff = fminf(fmaxf(cutoff, 0.0f), 1.0f);
    return true;
}

/* A move is spread over the next update (steady spawns along the way, velocity to
   inherit); a jump, or the first position, isn't a move. */
static void move_to(wgr_emitter_t *emitter_ptr, vec3_t position, bool jump)
{
    emitter_ptr->position = position;
    if (jump || !emitter_ptr->placed) {
        emitter_ptr->previous = position;
        emitter_ptr->movement_velocity = (vec3_t){0, 0, 0};
    }
    emitter_ptr->placed = true;
}

/* A key into a curve, in order of time (after any at the same time: a step). */
static bool insert_key(float *times, int *count, float t, int *at)
{
    int i;
    if (*count >= MAX_KEYS || !(t >= 0.0f && t <= 1.0f)) return false;
    for (i = *count; i > 0 && times[i - 1] > t; i--) times[i] = times[i - 1];
    times[i] = t;
    (*count)++;
    *at = i;
    return true;
}

static bool add_size_key(wgr_emitter_t *emitter_ptr, float t, float size)
{
    int at, n = emitter_ptr->size_keys;
    if (!(size >= 0.0f) || !insert_key(emitter_ptr->size_times, &emitter_ptr->size_keys, t, &at)) return false;
    memmove(&emitter_ptr->size_values[at + 1], &emitter_ptr->size_values[at], sizeof(float) * (size_t)(n - at));
    emitter_ptr->size_values[at] = size;
    return true;
}

static bool add_color_key(wgr_emitter_t *emitter_ptr, float t, wgr_color_t color)
{
    int at, n = emitter_ptr->color_keys;
    if (!insert_key(emitter_ptr->color_times, &emitter_ptr->color_keys, t, &at)) return false;
    memmove(&emitter_ptr->color_values[at + 1], &emitter_ptr->color_values[at], sizeof(wgr_color_t) * (size_t)(n - at));
    emitter_ptr->color_values[at] = color;
    return true;
}

static bool set_size(wgr_emitter_t *emitter_ptr, float start, float end, float variance)
{
    if (!(start >= 0.0f) || !(end >= 0.0f)) return false;
    emitter_ptr->size_keys = 0;
    add_size_key(emitter_ptr, 0.0f, start);
    add_size_key(emitter_ptr, 1.0f, end);
    emitter_ptr->size_variance = fminf(fmaxf(variance, 0.0f), 1.0f);
    return true;
}

static void set_color(wgr_emitter_t *emitter_ptr, wgr_color_t start, wgr_color_t end)
{
    emitter_ptr->color_keys = 0;
    add_color_key(emitter_ptr, 0.0f, start);
    add_color_key(emitter_ptr, 1.0f, end);
}

static bool add_palette_color(wgr_emitter_t *emitter_ptr, wgr_color_t color)
{
    if (emitter_ptr->palette_count >= MAX_PALETTE) return false;
    emitter_ptr->palette[emitter_ptr->palette_count++] = color;
    return true;
}

static bool set_frames(wgr_emitter_t *emitter_ptr, int columns, int rows, int count, float per_second)
{
    if (columns < 1 || rows < 1 || columns * rows > 4096 || count > columns * rows || !(per_second >= 0.0f)) {
        return false;
    }
    emitter_ptr->columns = columns;
    emitter_ptr->rows = rows;
    emitter_ptr->frames = count > 0 ? count : columns * rows;
    emitter_ptr->frames_per_second = per_second;
    return true;
}

/* Start over as if the steady rate had been running for `seconds`: particles born over
 * that time (as many as can still be alive, and fit), where the emitter is now. */
static bool prewarm(wgr_emitter_t *emitter_ptr, float seconds)
{
    float window;
    int count;
    if (!(seconds >= 0.0f)) return false;
    emitter_ptr->live = 0;
    emitter_ptr->next = 0;
    if (!emitter_ptr->emitting || emitter_ptr->rate <= 0.0f) return true;
    window = fminf(seconds, emitter_ptr->life_max);
    count = (int)(emitter_ptr->rate * window);
    if (count > emitter_ptr->max) count = emitter_ptr->max; /* the newest */
    for (int k = 0; k < count; k++) {
        spawn(emitter_ptr, emitter_ptr->time - (float)(count - 1 - k) / emitter_ptr->rate, emitter_ptr->position);
    }
    retire(emitter_ptr);
    return true;
}

static bool burst(wgr_emitter_t *emitter_ptr, int count)
{
    if (count < 0) return false;
    if (count > emitter_ptr->max) count = emitter_ptr->max;
    for (int i = 0; i < count; i++) spawn(emitter_ptr, emitter_ptr->time, emitter_ptr->position);
    return true;
}

WGRI_KEEP wgr_handle_t wgr_emitter3d_create(wgr_handle_t texture) { return create_emitter(texture, false); }
WGRI_KEEP wgr_handle_t wgr_emitter2d_create(wgr_handle_t texture) { return create_emitter(texture, true); }
WGRI_KEEP void wgr_emitter3d_destroy(wgr_handle_t emitter) { destroy_emitter(emitter, false); }
WGRI_KEEP void wgr_emitter2d_destroy(wgr_handle_t emitter) { destroy_emitter(emitter, true); }

WGRI_KEEP bool wgr_emitter3d_set_source(wgr_handle_t e, float x, float y, float w, float h)
{
    WITH_EMITTER(e, false, (emitter_ptr->source[0] = x, emitter_ptr->source[1] = y, emitter_ptr->source[2] = w,
                            emitter_ptr->source[3] = h));
}
WGRI_KEEP bool wgr_emitter2d_set_source(wgr_handle_t e, float x, float y, float w, float h)
{
    WITH_EMITTER(e, true, (emitter_ptr->source[0] = x, emitter_ptr->source[1] = y, emitter_ptr->source[2] = w,
                           emitter_ptr->source[3] = h));
}
WGRI_KEEP bool wgr_emitter3d_set_position(wgr_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, move_to(emitter_ptr, (vec3_t){x, y, z}, false));
}
WGRI_KEEP bool wgr_emitter2d_set_position(wgr_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, move_to(emitter_ptr, (vec3_t){x, y, 0.0f}, false));
}
WGRI_KEEP bool wgr_emitter3d_jump(wgr_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, move_to(emitter_ptr, (vec3_t){x, y, z}, true));
}
WGRI_KEEP bool wgr_emitter2d_jump(wgr_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, move_to(emitter_ptr, (vec3_t){x, y, 0.0f}, true));
}
WGRI_KEEP vec3_t wgr_emitter3d_get_position(wgr_handle_t e)
{
    const wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL ? emitter_ptr->position : (vec3_t){0, 0, 0};
}
WGRI_KEEP vec2_t wgr_emitter2d_get_position(wgr_handle_t e)
{
    const wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL ? (vec2_t){emitter_ptr->position.x, emitter_ptr->position.y} : (vec2_t){0, 0};
}
WGRI_KEEP bool wgr_emitter3d_set_rate(wgr_handle_t e, float per_second)
{
    if (per_second < 0.0f) return false;
    WITH_EMITTER(e, false, emitter_ptr->rate = per_second);
}
WGRI_KEEP bool wgr_emitter2d_set_rate(wgr_handle_t e, float per_second)
{
    if (per_second < 0.0f) return false;
    WITH_EMITTER(e, true, emitter_ptr->rate = per_second);
}
WGRI_KEEP bool wgr_emitter3d_burst(wgr_handle_t e, int count)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && burst(emitter_ptr, count);
}
WGRI_KEEP bool wgr_emitter2d_burst(wgr_handle_t e, int count)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && burst(emitter_ptr, count);
}
WGRI_KEEP bool wgr_emitter3d_set_emitting(wgr_handle_t e, bool emitting)
{
    WITH_EMITTER(e, false, emitter_ptr->emitting = emitting);
}
WGRI_KEEP bool wgr_emitter2d_set_emitting(wgr_handle_t e, bool emitting)
{
    WITH_EMITTER(e, true, emitter_ptr->emitting = emitting);
}
WGRI_KEEP bool wgr_emitter3d_is_emitting(wgr_handle_t e)
{
    const wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && emitter_ptr->emitting;
}
WGRI_KEEP bool wgr_emitter2d_is_emitting(wgr_handle_t e)
{
    const wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && emitter_ptr->emitting;
}
WGRI_KEEP bool wgr_emitter3d_set_max(wgr_handle_t e, int count) { return set_max(resolve_kind(e, false), count); }
WGRI_KEEP bool wgr_emitter2d_set_max(wgr_handle_t e, int count) { return set_max(resolve_kind(e, true), count); }
WGRI_KEEP bool wgr_emitter3d_set_life(wgr_handle_t e, float min_s, float max_s)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_life(emitter_ptr, min_s, max_s);
}
WGRI_KEEP bool wgr_emitter2d_set_life(wgr_handle_t e, float min_s, float max_s)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_life(emitter_ptr, min_s, max_s);
}
WGRI_KEEP bool wgr_emitter3d_set_spawn_box(wgr_handle_t e, float hx, float hy, float hz)
{
    WITH_EMITTER(e, false, (emitter_ptr->box = (vec3_t){fabsf(hx), fabsf(hy), fabsf(hz)}, emitter_ptr->sphere = 0));
}
WGRI_KEEP bool wgr_emitter2d_set_spawn_box(wgr_handle_t e, float hw, float hh)
{
    WITH_EMITTER(e, true, (emitter_ptr->box = (vec3_t){fabsf(hw), fabsf(hh), 0.0f}, emitter_ptr->sphere = 0));
}
WGRI_KEEP bool wgr_emitter3d_set_spawn_sphere(wgr_handle_t e, float radius)
{
    if (!(radius >= 0.0f)) return false;
    WITH_EMITTER(e, false, (emitter_ptr->sphere = radius, emitter_ptr->box = (vec3_t){0, 0, 0}));
}
WGRI_KEEP bool wgr_emitter2d_set_spawn_circle(wgr_handle_t e, float radius)
{
    if (!(radius >= 0.0f)) return false;
    WITH_EMITTER(e, true, (emitter_ptr->sphere = radius, emitter_ptr->box = (vec3_t){0, 0, 0}));
}
WGRI_KEEP bool wgr_emitter3d_set_frames(wgr_handle_t e, int columns, int rows, int count, float per_second)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_frames(emitter_ptr, columns, rows, count, per_second);
}
WGRI_KEEP bool wgr_emitter2d_set_frames(wgr_handle_t e, int columns, int rows, int count, float per_second)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_frames(emitter_ptr, columns, rows, count, per_second);
}
WGRI_KEEP bool wgr_emitter3d_prewarm(wgr_handle_t e, float seconds)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && prewarm(emitter_ptr, seconds);
}
WGRI_KEEP bool wgr_emitter2d_prewarm(wgr_handle_t e, float seconds)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && prewarm(emitter_ptr, seconds);
}
WGRI_KEEP bool wgr_emitter3d_set_velocity(wgr_handle_t e, float x, float y, float z, float spread, float speed_variance)
{
    WITH_EMITTER(e, false, (emitter_ptr->velocity = (vec3_t){x, y, z}, emitter_ptr->spread = fmaxf(spread, 0.0f),
                            emitter_ptr->speed_variance = fminf(fmaxf(speed_variance, 0.0f), 1.0f)));
}
WGRI_KEEP bool wgr_emitter2d_set_velocity(wgr_handle_t e, float x, float y, float spread, float speed_variance)
{
    WITH_EMITTER(e, true, (emitter_ptr->velocity = (vec3_t){x, y, 0.0f}, emitter_ptr->spread = fmaxf(spread, 0.0f),
                           emitter_ptr->speed_variance = fminf(fmaxf(speed_variance, 0.0f), 1.0f)));
}
WGRI_KEEP bool wgr_emitter3d_set_gravity(wgr_handle_t e, float x, float y, float z)
{
    WITH_EMITTER(e, false, emitter_ptr->gravity = ((vec3_t){x, y, z}));
}
WGRI_KEEP bool wgr_emitter2d_set_gravity(wgr_handle_t e, float x, float y)
{
    WITH_EMITTER(e, true, emitter_ptr->gravity = ((vec3_t){x, y, 0.0f}));
}
WGRI_KEEP bool wgr_emitter3d_set_drag(wgr_handle_t e, float per_second)
{
    if (!(per_second >= 0.0f)) return false;
    WITH_EMITTER(e, false, emitter_ptr->drag = per_second);
}
WGRI_KEEP bool wgr_emitter2d_set_drag(wgr_handle_t e, float per_second)
{
    if (!(per_second >= 0.0f)) return false;
    WITH_EMITTER(e, true, emitter_ptr->drag = per_second);
}
WGRI_KEEP bool wgr_emitter3d_set_stretch(wgr_handle_t e, float seconds)
{
    if (!(seconds >= 0.0f)) return false;
    WITH_EMITTER(e, false, emitter_ptr->stretch = seconds);
}
WGRI_KEEP bool wgr_emitter2d_set_stretch(wgr_handle_t e, float seconds)
{
    if (!(seconds >= 0.0f)) return false;
    WITH_EMITTER(e, true, emitter_ptr->stretch = seconds);
}
WGRI_KEEP bool wgr_emitter3d_set_inherit_velocity(wgr_handle_t e, float fraction)
{
    WITH_EMITTER(e, false, emitter_ptr->inherit = fraction);
}
WGRI_KEEP bool wgr_emitter2d_set_inherit_velocity(wgr_handle_t e, float fraction)
{
    WITH_EMITTER(e, true, emitter_ptr->inherit = fraction);
}
WGRI_KEEP bool wgr_emitter3d_set_size(wgr_handle_t e, float start, float end, float variance)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_size(emitter_ptr, start, end, variance);
}
WGRI_KEEP bool wgr_emitter2d_set_size(wgr_handle_t e, float start, float end, float variance)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_size(emitter_ptr, start, end, variance);
}
WGRI_KEEP bool wgr_emitter3d_add_size_key(wgr_handle_t e, float t, float size)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && add_size_key(emitter_ptr, t, size);
}
WGRI_KEEP bool wgr_emitter2d_add_size_key(wgr_handle_t e, float t, float size)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && add_size_key(emitter_ptr, t, size);
}
WGRI_KEEP bool wgr_emitter3d_clear_size_keys(wgr_handle_t e) { WITH_EMITTER(e, false, emitter_ptr->size_keys = 0); }
WGRI_KEEP bool wgr_emitter2d_clear_size_keys(wgr_handle_t e) { WITH_EMITTER(e, true, emitter_ptr->size_keys = 0); }
WGRI_KEEP bool wgr_emitter3d_set_color(wgr_handle_t e, wgr_color_t start, wgr_color_t end)
{
    WITH_EMITTER(e, false, set_color(emitter_ptr, start, end));
}
WGRI_KEEP bool wgr_emitter2d_set_color(wgr_handle_t e, wgr_color_t start, wgr_color_t end)
{
    WITH_EMITTER(e, true, set_color(emitter_ptr, start, end));
}
WGRI_KEEP bool wgr_emitter3d_add_color_key(wgr_handle_t e, float t, wgr_color_t color)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && add_color_key(emitter_ptr, t, color);
}
WGRI_KEEP bool wgr_emitter2d_add_color_key(wgr_handle_t e, float t, wgr_color_t color)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && add_color_key(emitter_ptr, t, color);
}
WGRI_KEEP bool wgr_emitter3d_clear_color_keys(wgr_handle_t e) { WITH_EMITTER(e, false, emitter_ptr->color_keys = 0); }
WGRI_KEEP bool wgr_emitter2d_clear_color_keys(wgr_handle_t e) { WITH_EMITTER(e, true, emitter_ptr->color_keys = 0); }
WGRI_KEEP bool wgr_emitter3d_add_palette_color(wgr_handle_t e, wgr_color_t color)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && add_palette_color(emitter_ptr, color);
}
WGRI_KEEP bool wgr_emitter2d_add_palette_color(wgr_handle_t e, wgr_color_t color)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && add_palette_color(emitter_ptr, color);
}
WGRI_KEEP bool wgr_emitter3d_clear_palette(wgr_handle_t e) { WITH_EMITTER(e, false, emitter_ptr->palette_count = 0); }
WGRI_KEEP bool wgr_emitter2d_clear_palette(wgr_handle_t e) { WITH_EMITTER(e, true, emitter_ptr->palette_count = 0); }
WGRI_KEEP bool wgr_emitter3d_set_spin(wgr_handle_t e, float min, float max)
{
    WITH_EMITTER(e, false, (emitter_ptr->spin_min = min, emitter_ptr->spin_max = max));
}
WGRI_KEEP bool wgr_emitter2d_set_spin(wgr_handle_t e, float min, float max)
{
    WITH_EMITTER(e, true, (emitter_ptr->spin_min = min, emitter_ptr->spin_max = max));
}
WGRI_KEEP bool wgr_emitter3d_set_alpha_mode(wgr_handle_t e, wgr_alpha_mode_t mode, float cutoff)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    return emitter_ptr != NULL && set_alpha_mode(emitter_ptr, mode, cutoff);
}
WGRI_KEEP bool wgr_emitter2d_set_alpha_mode(wgr_handle_t e, wgr_alpha_mode_t mode, float cutoff)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    return emitter_ptr != NULL && set_alpha_mode(emitter_ptr, mode, cutoff);
}
WGRI_KEEP bool wgr_emitter3d_set_seed(wgr_handle_t e, unsigned int seed)
{
    WITH_EMITTER(e, false, emitter_ptr->rng = seed != 0 ? seed : 1u);
}
WGRI_KEEP bool wgr_emitter2d_set_seed(wgr_handle_t e, unsigned int seed)
{
    WITH_EMITTER(e, true, emitter_ptr->rng = seed != 0 ? seed : 1u);
}
WGRI_KEEP int wgr_emitter3d_get_count(wgr_handle_t e) { return count_alive(resolve_kind(e, false)); }
WGRI_KEEP int wgr_emitter2d_get_count(wgr_handle_t e) { return count_alive(resolve_kind(e, true)); }
WGRI_KEEP void wgr_emitter3d_clear(wgr_handle_t e)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, false);
    if (emitter_ptr != NULL) emitter_ptr->live = 0;
}
WGRI_KEEP void wgr_emitter2d_clear(wgr_handle_t e)
{
    wgr_emitter_t *emitter_ptr = resolve_kind(e, true);
    if (emitter_ptr != NULL) emitter_ptr->live = 0;
}
WGRI_KEEP bool wgr_emitter3d_set_visible(wgr_handle_t e, bool visible)
{
    WITH_EMITTER(e, false, emitter_ptr->visible = visible);
}
WGRI_KEEP bool wgr_emitter2d_set_visible(wgr_handle_t e, bool visible)
{
    WITH_EMITTER(e, true, emitter_ptr->visible = visible);
}
WGRI_KEEP void wgr_emitter3d_draw(wgr_handle_t e)
{
    if (resolve_kind(e, false) != NULL) draw_emitter(e);
}
WGRI_KEEP void wgr_emitter2d_draw(wgr_handle_t e)
{
    if (resolve_kind(e, true) != NULL) draw_emitter(e);
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgri_module.h). */
static wgri_module_t wgr_emitter_module = {.name = "emitter", .order = 70, .init = wgri_emitter_init, .deinit = wgri_emitter_deinit, .update = wgri_emitter_update, .flush = wgri_emitter_flush, .end_frame = wgri_emitter_end_frame};
WGRI_MODULE(wgr_emitter_module)
