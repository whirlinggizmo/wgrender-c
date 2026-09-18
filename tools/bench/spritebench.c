/* Sprite benchmark (docs/PLAN-2d.md, "Not in this plan: a batched 2D renderer"):
 * how far sprite-heavy scenes go before something breaks, and what breaks first —
 * a pool running out, sokol_gl's per-frame budgets overflowing (which drops draws
 * rather than slowing down), or frame time.
 *
 * For each scene and sprite count it reports how many sprites could be created,
 * mean and worst frame time, the CPU time spent in the frame callback, and
 * sokol_gl's vertex and command use with any overflow.
 *
 * Pools and sokol_gl's budgets grow as needed, so past the warm-up frames nothing
 * should overflow up to the most they can grow to.
 *
 *   make spritebench            headless: CPU cost only, default then large budgets
 *   make spritebench DESKTOP=1  desktop, vsync off: real GPU cost (opens a window)
 *
 * The second run links a library whose sokol_gl budgets start large instead of
 * growing (BENCH_DEFS in the Makefile), in its own build directory. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sk.h"

#include "sokol_gfx.h"
#include "util/sokol_gl.h"

#define ASSET_BASE "examples/assets"
#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_BENCH_SPRITES 40000

#ifndef SK_SGL_VERTICES
#define SK_SGL_VERTICES 0
#endif
#ifndef SK_SGL_COMMANDS
#define SK_SGL_COMMANDS 0
#endif

enum { TEXTURES = 4 };
static const char *TEXTURE_PATHS[TEXTURES] = {"textures/tiles.png", "textures/ui_panel.png",
                                              "textures/blobshadow.png", "sprites/logo/wg-logo-bw-alpha.png"};

typedef enum { SCENE_SPRITE3D_ATLAS, SCENE_SPRITE3D_MIXED, SCENE_SPRITE2D_ATLAS } scene_kind_t;
static const char *SCENE_NAMES[] = {"sprite3d, one atlas", "sprite3d, 4 textures", "sprite2d, one atlas"};

static const int COUNTS[] = {1024, 4096, 16384, 32768};
enum { COUNT_STEPS = sizeof(COUNTS) / sizeof(COUNTS[0]), SCENES = 3 };

typedef struct {
    int wanted, created;
    double frame_mean, frame_worst, cpu_mean, cpu_worst;
    int vertices, commands;
    bool vertices_full, commands_full, other_error;
} result_t;

static struct {
    sk_handle_t textures[TEXTURES];
    int textures_loaded;
    bool failed;
    sk_handle_t scene, camera;
    sk_handle_t sprites[MAX_BENCH_SPRITES];
    int step;  /* scene * COUNT_STEPS + count index */
    int frame; /* within the step */
    double last, frame_sum, cpu_sum, worst, cpu_worst;
    result_t results[SCENES * COUNT_STEPS];
} b;

static void on_texture(const char *path, void *user)
{
    const int i = (int)(intptr_t)user;
    b.textures[i] = sk_texture_create(path);
    b.textures_loaded++;
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    fprintf(stderr, "spritebench: could not load %s\n", path);
    b.failed = true;
}

static void teardown(void)
{
    const scene_kind_t kind = (scene_kind_t)(b.step / COUNT_STEPS);
    sk_scene_clear(b.scene);
    for (int i = 0; i < MAX_BENCH_SPRITES; i++) {
        if (b.sprites[i] == 0) continue;
        if (kind == SCENE_SPRITE2D_ATLAS) {
            sk_sprite2d_destroy(b.sprites[i]);
        } else {
            sk_sprite3d_destroy(b.sprites[i]);
        }
        b.sprites[i] = 0;
    }
}

/* Build the step's scene: N sprites in a square grid, all on screen. */
static void setup(void)
{
    const scene_kind_t kind = (scene_kind_t)(b.step / COUNT_STEPS);
    result_t *r = &b.results[b.step];
    const int n = COUNTS[b.step % COUNT_STEPS];
    const int side = (int)ceil(sqrt((double)n));
    const vec2_t screen = sk_window_get_screen_size();

    r->wanted = n;
    r->created = 0;
    sk_camera3d_set_view(b.camera, side * 0.5f, side * 0.5f, 10.0f, side * 0.5f, side * 0.5f, 0.0f, 0, 1, 0);
    sk_camera3d_set_ortho_height(b.camera, (float)side);
    for (int i = 0; i < n; i++) {
        const float x = (float)(i % side), y = (float)(i / side);
        sk_handle_t sprite;
        if (kind == SCENE_SPRITE2D_ATLAS) {
            const float cell = screen.y / (float)side;
            sprite = sk_sprite2d_create(b.textures[0]);
            if (sprite == 0) break; /* pool full */
            sk_sprite2d_set_source(sprite, (float)(16 * (i % 4)), 0, 16, 16);
            sk_sprite2d_set_size(sprite, cell, cell);
            sk_sprite2d_set_pivot(sprite, 0, 0);
            sk_sprite2d_set_position(sprite, x * cell, y * cell);
        } else {
            const sk_handle_t texture = kind == SCENE_SPRITE3D_MIXED ? b.textures[i % TEXTURES] : b.textures[0];
            sprite = sk_sprite3d_create(texture);
            if (sprite == 0) break; /* pool full */
            sk_sprite3d_set_facing(sprite, SK_SPRITE3D_FACING_FREE);
            if (kind == SCENE_SPRITE3D_ATLAS) {
                sk_sprite3d_set_source(sprite, (float)(16 * (i % 4)), 0, 16, 16);
            }
            sk_sprite3d_set_transform(sprite, x + 0.5f, y + 0.5f, 0, 0, 0, 0, 1, 1, 1);
        }
        b.sprites[i] = sprite;
        sk_scene_add(b.scene, sprite, 0);
        r->created++;
    }
    b.frame = 0;
    b.frame_sum = b.cpu_sum = b.worst = b.cpu_worst = 0.0;
}

static void print_results(void)
{
#if defined(SK_HEADLESS)
    /* without a display the runtime paces frames at a stand-in 60 Hz (src/sk.c), so
       frame-to-frame time is that interval, not a measurement: report CPU only */
    const bool frames = false;
    const char *build = "headless: CPU only; frames are paced at a stand-in 60 Hz";
#else
    const bool frames = true;
    const char *build = "desktop, vsync off";
#endif
    printf("\nspritebench (%s; %s)\n", build, SK_SGL_VERTICES > 0 ? "large starting budgets" : "default budgets");
    if (SK_SGL_VERTICES > 0) {
        printf("sokol_gl budget: starts at %d vertices, %d commands per frame\n", SK_SGL_VERTICES, SK_SGL_COMMANDS);
    } else {
        printf("sokol_gl budget: starts at 65536 vertices, 16384 commands per frame, doubling as needed\n");
    }
    printf("%-22s %7s %8s", "scene", "wanted", "created");
    if (frames) printf(" %9s %9s", "frame ms", "worst ms");
    printf(" %8s %8s %9s %9s  %s\n", "cpu ms", "cpu max", "vertices", "commands", "overflow");
    for (int i = 0; i < SCENES * COUNT_STEPS; i++) {
        const result_t *r = &b.results[i];
        char overflow[64] = "-";
        if (r->vertices_full || r->commands_full || r->other_error) {
            snprintf(overflow, sizeof(overflow), "%s%s%s", r->vertices_full ? "vertices " : "",
                     r->commands_full ? "commands " : "", r->other_error ? "other" : "");
        }
        printf("%-22s %7d %8d", SCENE_NAMES[i / COUNT_STEPS], r->wanted, r->created);
        if (frames) printf(" %9.2f %9.2f", r->frame_mean, r->frame_worst);
        printf(" %8.2f %8.2f %9d %9d  %s\n", r->cpu_mean, r->cpu_worst, r->vertices, r->commands, overflow);
    }
    fflush(stdout);
}

static void init(void *user)
{
    (void)user;
    sk_asset_set_host(ASSET_BASE);
    sk_set_target_fps(0);
    b.camera = sk_camera3d_create(SK_CAMERA3D_ORTHOGRAPHIC);
    b.scene = sk_scene_create();
    sk_scene_set_active_camera(b.scene, b.camera);
    b.step = -1; /* set up once the textures are in */
    for (int i = 0; i < TEXTURES; i++) {
        sk_asset_add_task(sk_asset_ensure_async(TEXTURE_PATHS[i], NULL, SK_ASSET_NONE), on_texture, on_failed,
                          (void *)(intptr_t)i);
    }
}

static void frame(float dt, float fraction, void *user)
{
    const double start = sk_get_time();
    result_t *r;
    (void)dt;
    (void)fraction;
    (void)user;

    if (b.failed) {
        sk_request_quit();
        return;
    }
    if (b.step < 0) { /* waiting for textures */
        if (b.textures_loaded == TEXTURES) {
            b.step = 0;
            setup();
            b.last = sk_get_time();
        }
        sk_render_begin();
        sk_render_end();
        return;
    }

    r = &b.results[b.step];
    sk_render_begin();
    sk_render_clear_background(SK_COLOR_BLACK);
    sk_scene_draw(b.scene);
    if (b.frame >= WARMUP_FRAMES) { /* read sokol_gl's use before the frame is submitted */
        const sgl_error_t err = sgl_error();
        const int vertices = sgl_num_vertices(), commands = sgl_num_commands();
        if (vertices > r->vertices) r->vertices = vertices;
        if (commands > r->commands) r->commands = commands;
        r->vertices_full = r->vertices_full || err.vertices_full;
        r->commands_full = r->commands_full || err.commands_full;
        r->other_error = r->other_error || err.uniforms_full || err.stack_overflow || err.no_context;
    }
    sk_render_end();

    if (b.frame >= WARMUP_FRAMES) {
        const double now = sk_get_time(), frame_time = (now - b.last) * 1000.0, cpu = (now - start) * 1000.0;
        b.frame_sum += frame_time;
        b.cpu_sum += cpu;
        if (frame_time > b.worst) b.worst = frame_time;
        if (cpu > b.cpu_worst) b.cpu_worst = cpu;
    }
    b.last = sk_get_time();

    if (++b.frame == WARMUP_FRAMES + MEASURE_FRAMES) {
        r->frame_mean = b.frame_sum / MEASURE_FRAMES;
        r->cpu_mean = b.cpu_sum / MEASURE_FRAMES;
        r->frame_worst = b.worst;
        r->cpu_worst = b.cpu_worst;
        teardown();
        if (++b.step == SCENES * COUNT_STEPS) {
            print_results();
            sk_request_quit();
            return;
        }
        setup();
        b.last = sk_get_time();
    }
}

int main(void)
{
    sk_init_values(960, 600, "libsk spritebench", SK_WINDOW_FLAG_VSYNC_OFF);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
