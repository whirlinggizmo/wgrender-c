/* Stress scene: game logic at scale, for comparing the bindings (tools/benchmarks.py).
 *
 * `simple` measures what a binding costs; this measures what a game's own frame costs
 * in each language: N entities updated every frame, each one wgr call, a steady churn
 * of entities dying and being replaced (objects created and destroyed, and in a managed
 * language, allocated and collected), and a screenful of formatted text. The bindings
 * port it line for line (wgrender-hx: examples/stress, as a JS guest and through hxcpp;
 * wgrender-nim and wgrender-beef: examples/stress), each written the way that language
 * naturally would: C reuses array slots, Nim makes a new ref object under ARC, Beef a
 * new class instance it deletes itself, Haxe a new class instance under the GC.
 *
 * The spec, which the ports follow exactly so every language does the same work:
 *
 *   N           ?n= in the page's URL on the web, argv[1] (or STRESS_N) on desktop;
 *               default 2000
 *   step        a fixed 1/60 s per frame, whatever the frame's dt, so the work per
 *               frame is identical across languages and runs
 *   random      xorshift32 on an unsigned 32-bit state, seeded 2463534242:
 *                   x ^= x << 13; x ^= x >> 17; x ^= x << 5    (logical shifts)
 *               rand() = (x >> 8) / 16777216, in [0, 1)
 *   spawn       in this order: x = (r*2-1)*BOX/2, y = 1 + r*4, z = (r*2-1)*BOX/2,
 *               vx = (r*2-1)*4, vy = 4 + r*6, vz = (r*2-1)*4, spin = (r*2-1)*3,
 *               life = 2 + r*4; angle = 0. A new sprite3d of the logo texture,
 *               facing FREE, added to the scene.
 *   update      vy -= 9.8*step; x += vx*step; y += vy*step; z += vz*step;
 *               y < 0: y = 0, vy = -vy*0.8; |x| > BOX: x = +-BOX, vx = -vx (z the
 *               same); angle += spin*step; life -= step; then
 *               wgr_sprite3d_set_transform(x, y, z, 0, angle, 0, 0.5, 0.5, 0.5).
 *               life <= 0: destroy the sprite and spawn a new entity in its place.
 *               (2-6 s lives: about N/240 replaced a frame.)
 *   text        each frame, with the built-in font at 16 px, black: "stress: N
 *               entities" at (10, 10), then for the first 48 entities
 *               "e<i>: <x> <y> <z> life <life>", every number to 2 decimals, at
 *               (10, 34 + 18*i)
 *   camera      perspective, from (0, 14, 30) at (0, 3, 0); BOX = 10
 *
 *   the stress target of a web preset: a page (out/web/<variant>/bench/),
 *   /bench/?ex=stress&n=5000
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "wgr.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define ASSET_BASE "/assets"
#else
#define ASSET_BASE "examples/assets"
#endif

#define SPRITE_PATH "sprites/logo/wg-logo-bw-alpha.png"
#define DEFAULT_N 2000
#define STEP (1.0f / 60.0f)
#define BOX 10.0f
#define TEXT_LINES 48

typedef struct {
    float x, y, z, vx, vy, vz, angle, spin, life;
    wgr_handle_t sprite;
} entity_t;

static struct {
    int n;
    uint32_t rng;
    entity_t *entities;
    wgr_handle_t texture;
    wgr_handle_t camera;
    wgr_handle_t scene;
    wgr_color_t background;
} g;

static float rnd(void)
{
    uint32_t x = g.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g.rng = x;
    return (float)(x >> 8) / 16777216.0f;
}

static void spawn(entity_t *e)
{
    e->x = (rnd() * 2 - 1) * BOX / 2;
    e->y = 1 + rnd() * 4;
    e->z = (rnd() * 2 - 1) * BOX / 2;
    e->vx = (rnd() * 2 - 1) * 4;
    e->vy = 4 + rnd() * 6;
    e->vz = (rnd() * 2 - 1) * 4;
    e->spin = (rnd() * 2 - 1) * 3;
    e->life = 2 + rnd() * 4;
    e->angle = 0;
    e->sprite = wgr_sprite3d_create(g.texture);
    wgr_sprite3d_set_facing(e->sprite, WGR_SPRITE3D_FACING_FREE);
    wgr_scene_add(g.scene, e->sprite, 0);
}

static void update(entity_t *e)
{
    e->vy -= 9.8f * STEP;
    e->x += e->vx * STEP;
    e->y += e->vy * STEP;
    e->z += e->vz * STEP;
    if (e->y < 0) {
        e->y = 0;
        e->vy = -e->vy * 0.8f;
    }
    if (fabsf(e->x) > BOX) {
        e->x = e->x > 0 ? BOX : -BOX;
        e->vx = -e->vx;
    }
    if (fabsf(e->z) > BOX) {
        e->z = e->z > 0 ? BOX : -BOX;
        e->vz = -e->vz;
    }
    e->angle += e->spin * STEP;
    e->life -= STEP;
    wgr_sprite3d_set_transform(e->sprite, e->x, e->y, e->z, 0, e->angle, 0, 0.5f, 0.5f, 0.5f);
    if (e->life <= 0) {
        wgr_sprite3d_destroy(e->sprite);
        spawn(e); /* C reuses the slot */
    }
}

static void on_texture_ready(const char *path, void *user)
{
    (void)user;
    g.texture = wgr_texture_create(path);
    g.entities = calloc((size_t)g.n, sizeof(entity_t));
    for (int i = 0; i < g.n; i++) {
        spawn(&g.entities[i]);
    }
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("failed to import asset: %s", path);
}

static void on_init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(ASSET_BASE);
    wgr_logger_set_level(WGR_LOGGER_LEVEL_WARN);
    wgr_set_target_fps(60);
    g.rng = 2463534242u;

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 14, 30, 0, 3, 0, 0, 1, 0);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    g.background = wgr_color_rgba(245, 245, 245, 255);

    wgr_handle_t task = wgr_asset_ensure_async(SPRITE_PATH, NULL, WGR_ASSET_NONE);
    if (wgr_asset_add_task(task, on_texture_ready, on_failed, NULL) != WGR_ASSET_ADD_TASK_OK) {
        on_failed(SPRITE_PATH, NULL);
    }
}

static void draw_text(void)
{
    char line[128];
    snprintf(line, sizeof(line), "stress: %d entities", g.n);
    wgr_text_draw(line, 10, 10, 16, WGR_COLOR_BLACK);
    if (g.entities == NULL) {
        return;
    }
    for (int i = 0; i < TEXT_LINES && i < g.n; i++) {
        const entity_t *e = &g.entities[i];
        snprintf(line, sizeof(line), "e%d: %.2f %.2f %.2f life %.2f", i, (double)e->x, (double)e->y,
                 (double)e->z, (double)e->life);
        wgr_text_draw(line, 10, 34 + 18 * i, 16, WGR_COLOR_BLACK);
    }
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    (void)dt;
    (void)tick_fraction;
    (void)user_data;
    if (g.entities != NULL) {
        for (int i = 0; i < g.n; i++) {
            update(&g.entities[i]);
        }
    }
    wgr_render_begin_frame();
    wgr_render_clear_background(g.background);
    wgr_scene_draw(g.scene);
    draw_text();
    wgr_render_end_frame();
}

static int entity_count(int argc, char **argv)
{
    int n = 0;
#ifdef __EMSCRIPTEN__
    (void)argc;
    (void)argv;
    n = emscripten_run_script_int("+(new URLSearchParams(location.search).get('n')) || 0");
#else
    const char *env = getenv("STRESS_N");
    n = argc > 1 ? atoi(argv[1]) : env != NULL ? atoi(env) : 0;
#endif
    return n > 0 ? n : DEFAULT_N;
}

int main(int argc, char **argv)
{
    g.n = entity_count(argc, argv);
    wgr_init_values(1024, 1280, "stress (libwgrender)", WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(on_init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
