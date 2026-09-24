/* Sprite benchmark: where the time goes in sprite-heavy frames, as a baseline for a
 * dedicated sprite renderer (docs/PLAN-sprites.md).
 *
 * Scenes, each at a few sprite counts:
 *   - grid:         sprite3d in a grid under an orthographic camera, facing FREE, one
 *                   atlas: the simplest case, for reference;
 *   - field:        sprite3d scattered on the ground under an orbiting perspective
 *                   camera, facings mixed as a game would (camera-facing coins,
 *                   upright trees that turn about Y, flat decals), blended and sorted
 *                   back to front each frame; from one atlas, or from 4 textures;
 *                   and the 4-texture field masked (WGR_ALPHA_MASK: not sorted);
 *   - particles 3d: camera-facing sprite3d thrown up and falling, each moved every
 *                   frame, 1/120 of them replaced every frame (a 2 s life); blended,
 *                   and additive (WGR_ALPHA_ADD: not sorted);
 *   - particles 2d: the same with sprite2d in screen space;
 *   - emitter 3d / 2d: the same particles from one emitter (wgr_emitter3d.h,
 *                   wgr_emitter2d.h), each written once at birth and moved by the GPU;
 *                   blended, additive, and 2D.
 *
 * For each it reports the sprites created, frame time (desktop), and CPU time in the
 * frame split into update (the benchmark's own calls: camera, particles), scene
 * (wgr_scene_draw: collect, sort, billboard, record) and submit (wgr_render_end_frame: upload
 * and draw), plus sokol_gl's vertices and draw commands and any overflow.
 *
 *   tools/bench/run.py spritebench             headless: CPU only
 *   tools/bench/run.py spritebench --desktop   desktop, vsync off: real GPU cost (opens a window)
 *   the spritebench target of a web preset     a page (out/web/<variant>/bench/); results
 *                               in the browser console; frames are paced by the display */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "wgr.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#ifdef __EMSCRIPTEN__
#define ASSET_BASE "/assets"
#else
#define ASSET_BASE "examples/assets"
#endif
#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_BENCH_SPRITES 16384
#define PARTICLE_LIFE 120 /* frames */

enum { TEXTURES = 4 };
static const char *TEXTURE_PATHS[TEXTURES] = {"textures/tiles.png", "textures/ui_panel.png",
                                              "textures/blobshadow.png", "sprites/logo/wg-logo-bw-alpha.png"};

typedef enum {
    SCENE_GRID,
    SCENE_FIELD_ATLAS,
    SCENE_FIELD_MIXED,
    SCENE_FIELD_MIXED_MASK,
    SCENE_PARTICLES_3D,
    SCENE_PARTICLES_3D_ADD,
    SCENE_PARTICLES_2D,
    SCENE_EMITTER_3D,
    SCENE_EMITTER_3D_ADD,
    SCENE_EMITTER_2D,
    SCENES
} scene_kind_t;
static const char *SCENE_NAMES[] = {"grid, ortho, atlas", "field, atlas", "field, 4 textures", "field, 4 tex, mask",
                                    "particles 3d",       "particles 3d, add", "particles 2d",
                                    "emitter 3d",         "emitter 3d, add",   "emitter 2d"};

static const int COUNTS[] = {1000, 4000, 16000};
enum { COUNT_STEPS = sizeof(COUNTS) / sizeof(COUNTS[0]) };

typedef struct {
    int wanted, created;
    double frame_mean, frame_worst, cpu_mean, cpu_worst, update_mean, scene_mean, submit_mean;
    int vertices, commands;
    bool vertices_full, commands_full, other_error;
} result_t;

typedef struct {
    float x, y, z, vx, vy, vz;
    int age;
} particle_t;

static struct {
    wgr_handle_t textures[TEXTURES];
    int textures_loaded;
    bool failed;
    wgr_handle_t scene, ortho, perspective;
    wgr_handle_t sprites[MAX_BENCH_SPRITES];
    wgr_handle_t emitter;
    particle_t particles[MAX_BENCH_SPRITES];
    int count;      /* sprites in the step */
    float radius;   /* of the field */
    unsigned seed;
    int step;       /* scene * COUNT_STEPS + count index */
    int frame;      /* within the step */
    double last, frame_sum, cpu_sum, update_sum, scene_sum, submit_sum, worst, cpu_worst;
    result_t results[SCENES * COUNT_STEPS];
} b;

static scene_kind_t current_kind(void)
{
    return (scene_kind_t)(b.step / COUNT_STEPS);
}

static float random01(void)
{
    b.seed = b.seed * 1664525u + 1013904223u;
    return (float)(b.seed >> 8) / 16777216.0f;
}

static void on_texture(const char *path, void *user)
{
    const int i = (int)(intptr_t)user;
    b.textures[i] = wgr_texture_create(path);
    b.textures_loaded++;
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    fprintf(stderr, "spritebench: could not load %s\n", path);
    b.failed = true;
}

static void destroy_sprite(int i)
{
    if (b.sprites[i] == 0) return;
    if (current_kind() == SCENE_PARTICLES_2D) {
        wgr_sprite2d_destroy(b.sprites[i]);
    } else {
        wgr_sprite3d_destroy(b.sprites[i]);
    }
    b.sprites[i] = 0;
}

static void teardown(void)
{
    wgr_scene_clear(b.scene);
    for (int i = 0; i < MAX_BENCH_SPRITES; i++) {
        destroy_sprite(i);
    }
    if (b.emitter != 0) {
        if (current_kind() == SCENE_EMITTER_2D) {
            wgr_emitter2d_destroy(b.emitter);
        } else {
            wgr_emitter3d_destroy(b.emitter);
        }
        b.emitter = 0;
    }
}

/* The particle scenes' particles from one emitter: as many alive, as long a life (2 s),
   thrown and pulled as fast. Burst full at once, so the steady state starts at once:
   from then on the rate replaces the oldest. */
static void add_emitter(int n)
{
    const float life = PARTICLE_LIFE / 60.0f;
    if (current_kind() == SCENE_EMITTER_2D) {
        const vec2_t screen = wgr_window_get_screen_size();
        b.emitter = wgr_emitter2d_create(b.textures[0]);
        if (b.emitter == 0) return;
        wgr_emitter2d_set_source(b.emitter, 32, 16, 16, 16); /* the coin */
        wgr_emitter2d_set_size(b.emitter, 10, 10, 0);
        wgr_emitter2d_set_position(b.emitter, screen.x * 0.5f, screen.y * 0.9f);
        wgr_emitter2d_set_velocity(b.emitter, 0, -540, 0.5f, 0.5f);
        wgr_emitter2d_set_gravity(b.emitter, 0, 540);
        wgr_emitter2d_set_alpha_mode(b.emitter, WGR_ALPHA_BLEND, 0);
        wgr_emitter2d_set_max(b.emitter, n);
        wgr_emitter2d_set_life(b.emitter, life, life);
        wgr_emitter2d_set_seed(b.emitter, 12345u);
        wgr_emitter2d_set_rate(b.emitter, (float)n / life);
        wgr_emitter2d_burst(b.emitter, n);
    } else {
        b.emitter = wgr_emitter3d_create(b.textures[0]);
        if (b.emitter == 0) return;
        wgr_emitter3d_set_source(b.emitter, 32, 16, 16, 16);
        wgr_emitter3d_set_size(b.emitter, 0.3f, 0.3f, 0);
        wgr_emitter3d_set_velocity(b.emitter, 0, 16, 0, 0.3f, 0.25f);
        wgr_emitter3d_set_gravity(b.emitter, 0, -18, 0);
        wgr_emitter3d_set_alpha_mode(b.emitter, current_kind() == SCENE_EMITTER_3D_ADD ? WGR_ALPHA_ADD : WGR_ALPHA_BLEND,
                                    0);
        wgr_emitter3d_set_max(b.emitter, n);
        wgr_emitter3d_set_life(b.emitter, life, life);
        wgr_emitter3d_set_seed(b.emitter, 12345u);
        wgr_emitter3d_set_rate(b.emitter, (float)n / life);
        wgr_emitter3d_burst(b.emitter, n);
    }
    wgr_scene_add(b.scene, b.emitter, 0);
}

/* A particle, new: from the fountain's mouth, thrown up and out. */
static void spawn_particle(int i, int age)
{
    particle_t *p = &b.particles[i];
    const float angle = random01() * 6.2831853f, speed = 0.5f + random01();
    wgr_handle_t sprite;

    if (current_kind() == SCENE_PARTICLES_2D) {
        const vec2_t screen = wgr_window_get_screen_size();
        *p = (particle_t){.x = screen.x * 0.5f, .y = screen.y * 0.9f, .vx = cosf(angle) * speed * 3.0f,
                          .vy = -(6.0f + random01() * 6.0f), .age = age};
        sprite = wgr_sprite2d_create(b.textures[0]);
        if (sprite == 0) return;
        wgr_sprite2d_set_source(sprite, 32, 16, 16, 16); /* the coin */
        wgr_sprite2d_set_size(sprite, 10, 10);
    } else {
        *p = (particle_t){.vx = cosf(angle) * speed * 0.05f, .vy = 0.2f + random01() * 0.15f,
                          .vz = sinf(angle) * speed * 0.05f, .age = age};
        sprite = wgr_sprite3d_create(b.textures[0]);
        if (sprite == 0) return;
        wgr_sprite3d_set_source(sprite, 32, 16, 16, 16);
        wgr_sprite3d_set_size(sprite, 0.3f);
        if (current_kind() == SCENE_PARTICLES_3D_ADD) {
            wgr_sprite3d_set_alpha_mode(sprite, WGR_ALPHA_ADD, 0);
        }
    }
    b.sprites[i] = sprite;
    wgr_scene_add(b.scene, sprite, 0);
    /* already on its way, so the steady state starts at once */
    for (int t = 0; t < age; t++) {
        p->x += p->vx, p->y += p->vy, p->z += p->vz;
        p->vy += current_kind() == SCENE_PARTICLES_2D ? 0.15f : -0.005f;
    }
}

static void add_field_sprite(int i)
{
    const float r = b.radius * sqrtf(random01()), angle = random01() * 6.2831853f;
    const float x = cosf(angle) * r, z = sinf(angle) * r;
    const bool mixed = current_kind() == SCENE_FIELD_MIXED || current_kind() == SCENE_FIELD_MIXED_MASK;
    const wgr_handle_t texture = mixed ? b.textures[i % TEXTURES] : b.textures[0];
    const wgr_handle_t sprite = wgr_sprite3d_create(texture);
    const bool atlas = !mixed;

    if (sprite == 0) return;
    switch (i % 3) {
        case 0: /* a coin: faces the camera */
            wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_CAMERA);
            if (atlas) wgr_sprite3d_set_source(sprite, 32, 16, 16, 16);
            wgr_sprite3d_set_transform(sprite, x, 0.6f, z, 0, 0, 0, 1, 1, 1);
            wgr_sprite3d_set_size(sprite, 0.6f);
            break;
        case 1: /* a tree: upright, turns about Y, stands on its bottom edge */
            wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_CAMERA_FIXED_Y);
            if (atlas) wgr_sprite3d_set_source(sprite, 0, 16, 16, 32);
            wgr_sprite3d_set_extent(sprite, 1, 2);
            wgr_sprite3d_set_pivot(sprite, 0.5f, 1);
            wgr_sprite3d_set_transform(sprite, x, 0, z, 0, 0, 0, 1, 1, 1);
            break;
        default: /* a decal flat on the ground */
            wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_Y_UP);
            if (atlas) wgr_sprite3d_set_source(sprite, 0, 0, 16, 16);
            wgr_sprite3d_set_transform(sprite, x, 0.01f, z, 0, 0, 0, 1, 1, 1);
            break;
    }
    if (current_kind() == SCENE_FIELD_MIXED_MASK) {
        wgr_sprite3d_set_alpha_mode(sprite, WGR_ALPHA_MASK, 0.5f);
    }
    b.sprites[i] = sprite;
    wgr_scene_add(b.scene, sprite, 0);
}

/* Build the step's scene. */
static void setup(void)
{
    const scene_kind_t kind = current_kind();
    result_t *r = &b.results[b.step];
    const int n = COUNTS[b.step % COUNT_STEPS];

    r->wanted = n;
    b.count = n;
    b.seed = 12345u;
    b.radius = sqrtf((float)n) * 0.6f;
    if (kind == SCENE_GRID) {
        const int side = (int)ceil(sqrt((double)n));
        wgr_scene_set_active_camera(b.scene, b.ortho);
        wgr_camera3d_set_view(b.ortho, side * 0.5f, side * 0.5f, 10.0f, side * 0.5f, side * 0.5f, 0.0f, 0, 1, 0);
        wgr_camera3d_set_ortho_height(b.ortho, (float)side);
        for (int i = 0; i < n; i++) {
            const wgr_handle_t sprite = wgr_sprite3d_create(b.textures[0]);
            if (sprite == 0) break;
            wgr_sprite3d_set_facing(sprite, WGR_SPRITE3D_FACING_FREE);
            wgr_sprite3d_set_source(sprite, (float)(16 * (i % 4)), 0, 16, 16);
            wgr_sprite3d_set_transform(sprite, (float)(i % side) + 0.5f, (float)(i / side) + 0.5f, 0, 0, 0, 0, 1, 1, 1);
            b.sprites[i] = sprite;
            wgr_scene_add(b.scene, sprite, 0);
        }
    } else if (kind == SCENE_FIELD_ATLAS || kind == SCENE_FIELD_MIXED || kind == SCENE_FIELD_MIXED_MASK) {
        wgr_scene_set_active_camera(b.scene, b.perspective);
        for (int i = 0; i < n; i++) add_field_sprite(i);
    } else {
        wgr_scene_set_active_camera(b.scene, b.perspective);
        wgr_camera3d_set_view(b.perspective, 0, 4, 12, 0, 3, 0, 0, 1, 0);
        if (kind >= SCENE_EMITTER_3D) {
            add_emitter(n);
        } else {
            for (int i = 0; i < n; i++) spawn_particle(i, i * PARTICLE_LIFE / n);
        }
    }
    r->created = 0;
    for (int i = 0; i < n; i++) r->created += b.sprites[i] != 0 ? 1 : 0;
    if (b.emitter != 0) {
        r->created = kind == SCENE_EMITTER_2D ? wgr_emitter2d_get_count(b.emitter) : wgr_emitter3d_get_count(b.emitter);
    }
    b.frame = 0;
    b.frame_sum = b.cpu_sum = b.update_sum = b.scene_sum = b.submit_sum = b.worst = b.cpu_worst = 0.0;
}

/* The benchmark's own per-frame work: the camera, and the particles. */
static void update(void)
{
    const scene_kind_t kind = current_kind();

    if (kind == SCENE_FIELD_ATLAS || kind == SCENE_FIELD_MIXED || kind == SCENE_FIELD_MIXED_MASK) {
        const float angle = (float)b.frame * 0.01f, distance = b.radius * 1.3f + 4.0f;
        wgr_camera3d_set_view(b.perspective, cosf(angle) * distance, b.radius * 0.35f + 2.0f, sinf(angle) * distance,
                             0, 0, 0, 0, 1, 0);
    } else if (kind == SCENE_PARTICLES_3D || kind == SCENE_PARTICLES_3D_ADD || kind == SCENE_PARTICLES_2D) {
        for (int i = 0; i < b.count; i++) {
            particle_t *p = &b.particles[i];
            if (b.sprites[i] == 0) continue;
            if (++p->age >= PARTICLE_LIFE) { /* replaced: destroyed and created anew */
                destroy_sprite(i);
                spawn_particle(i, 0);
                continue;
            }
            p->x += p->vx, p->y += p->vy, p->z += p->vz;
            if (kind == SCENE_PARTICLES_2D) {
                p->vy += 0.15f;
                wgr_sprite2d_set_position(b.sprites[i], p->x, p->y);
            } else {
                p->vy -= 0.005f;
                wgr_sprite3d_set_transform(b.sprites[i], p->x, p->y, p->z, 0, 0, 0, 1, 1, 1);
            }
        }
    }
}

static void print_results(void)
{
#if defined(WGR_HEADLESS)
    /* without a display the runtime paces frames at a stand-in 60 Hz (src/wgr.c), so
       frame-to-frame time is that interval, not a measurement: report CPU only */
    const bool frames = false;
    const char *build = "headless: CPU only";
#elif defined(__EMSCRIPTEN__)
    const bool frames = false; /* paced by the display */
    const char *build = "web: CPU only, frames paced by the display";
#else
    const bool frames = true;
    const char *build = "desktop, vsync off";
#endif
    printf("\nspritebench (%s)\n", build);
    printf("%-19s %6s %7s", "scene", "wanted", "created");
    if (frames) printf(" %8s %8s", "frame ms", "worst ms");
    printf(" %7s %7s %7s %7s %7s %8s %8s  %s\n", "cpu ms", "update", "scene", "submit", "cpu max", "vertices",
           "commands", "overflow");
    for (int i = 0; i < SCENES * COUNT_STEPS; i++) {
        const result_t *r = &b.results[i];
        char overflow[64] = "-";
        if (r->vertices_full || r->commands_full || r->other_error) {
            snprintf(overflow, sizeof(overflow), "%s%s%s", r->vertices_full ? "vertices " : "",
                     r->commands_full ? "commands " : "", r->other_error ? "other" : "");
        }
        printf("%-19s %6d %7d", SCENE_NAMES[i / COUNT_STEPS], r->wanted, r->created);
        if (frames) printf(" %8.2f %8.2f", r->frame_mean, r->frame_worst);
        printf(" %7.2f %7.2f %7.2f %7.2f %7.2f %8d %8d  %s\n", r->cpu_mean, r->update_mean, r->scene_mean,
               r->submit_mean, r->cpu_worst, r->vertices, r->commands, overflow);
    }
    printf("spritebench: done\n");
    fflush(stdout);
}

static void init(void *user)
{
    (void)user;
    wgr_asset_set_host(ASSET_BASE);
    wgr_set_target_fps(0);
    b.ortho = wgr_camera3d_create(WGR_CAMERA3D_ORTHOGRAPHIC);
    b.perspective = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    b.scene = wgr_scene_create();
    b.step = -1; /* set up once the textures are in */
    for (int i = 0; i < TEXTURES; i++) {
        wgr_asset_add_task(wgr_asset_ensure_async(TEXTURE_PATHS[i], NULL, WGR_ASSET_NONE), on_texture, on_failed,
                          (void *)(intptr_t)i);
    }
}

static void frame(float dt, float fraction, void *user)
{
    const double start = wgr_get_time();
    double updated, drawn, submitted;
    result_t *r;
    (void)dt;
    (void)fraction;
    (void)user;

    if (b.failed) {
        wgr_request_quit();
        return;
    }
    if (b.step < 0) { /* waiting for textures */
        if (b.textures_loaded == TEXTURES) {
            wgr_texture_set_sampling(b.textures[0], WGR_TEXTURE_WRAP_CLAMP, WGR_TEXTURE_WRAP_CLAMP,
                                    WGR_TEXTURE_FILTER_NEAREST);
            b.step = 0;
            setup();
            b.last = wgr_get_time();
        }
        wgr_render_begin_frame();
        wgr_render_end_frame();
        return;
    }

    r = &b.results[b.step];
    update();
    updated = wgr_get_time();
    wgr_render_begin_frame();
    wgr_render_clear_background(WGR_COLOR_BLACK);
    wgr_scene_draw(b.scene);
    drawn = wgr_get_time();
    if (b.frame >= WARMUP_FRAMES) { /* read sokol_gl's use before the frame is submitted */
        const sgl_error_t err = sgl_error();
        const int vertices = sgl_num_vertices(), commands = sgl_num_commands();
        if (vertices > r->vertices) r->vertices = vertices;
        if (commands > r->commands) r->commands = commands;
        r->vertices_full = r->vertices_full || err.vertices_full;
        r->commands_full = r->commands_full || err.commands_full;
        r->other_error = r->other_error || err.uniforms_full || err.stack_overflow || err.no_context;
    }
    wgr_render_end_frame();
    submitted = wgr_get_time();

    if (b.frame >= WARMUP_FRAMES) {
        const double frame_time = (submitted - b.last) * 1000.0, cpu = (submitted - start) * 1000.0;
        b.frame_sum += frame_time;
        b.cpu_sum += cpu;
        b.update_sum += (updated - start) * 1000.0;
        b.scene_sum += (drawn - updated) * 1000.0;
        b.submit_sum += (submitted - drawn) * 1000.0;
        if (frame_time > b.worst) b.worst = frame_time;
        if (cpu > b.cpu_worst) b.cpu_worst = cpu;
    }
    b.last = submitted;

    if (++b.frame == WARMUP_FRAMES + MEASURE_FRAMES) {
        r->frame_mean = b.frame_sum / MEASURE_FRAMES;
        r->cpu_mean = b.cpu_sum / MEASURE_FRAMES;
        r->update_mean = b.update_sum / MEASURE_FRAMES;
        r->scene_mean = b.scene_sum / MEASURE_FRAMES;
        r->submit_mean = b.submit_sum / MEASURE_FRAMES;
        r->frame_worst = b.worst;
        r->cpu_worst = b.cpu_worst;
        teardown();
        if (++b.step == SCENES * COUNT_STEPS) {
            print_results();
            wgr_request_quit();
            return;
        }
        setup();
        b.last = wgr_get_time();
    }
}

int main(void)
{
    wgr_init_values(960, 600, "libwgrender spritebench", WGR_WINDOW_FLAG_VSYNC_OFF | WGR_WINDOW_FLAG_LOW_DPI);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
