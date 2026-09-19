/* Loading benchmark (docs/PLAN-pipeline.md): loads Sponza and FlightHelmet during
 * a running frame loop, first through the asset pipeline (background), then
 * synchronously (files only ensured, meshes created in one callback), and prints
 * the worst frame and total time of each, then the worst of the first frames that
 * draw the loaded models. Needs tools/bench/fetch_assets.sh.
 *
 *   make loadbench            headless build: CPU work only (no GPU uploads)
 *   make loadbench DESKTOP=1  desktop build: real GL uploads (opens a window)
 *   make loadbench DESKTOP=1 KTX=1   the models with compressed textures
 *                             (tools/compress_textures.sh --gltf, made the first time;
 *                             SK_LOADBENCH_KTX=1 loads name.ktx.gltf) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sk.h"

#ifdef __EMSCRIPTEN__
#  define ASSET_BASE "/assets"
#else
#  define ASSET_BASE "examples/assets"
#endif

/* LOADBENCH_HELMET_ONLY: FlightHelmet alone (a phone: less to download);
 * LOADBENCH_KTX=1: compressed textures where there's no environment (the web) */
#ifdef LOADBENCH_HELMET_ONLY
enum { MODELS = 1 };
static const char *PATHS[MODELS] = {"bench/FlightHelmet/FlightHelmet.gltf"};
static const char *KTX_PATHS[MODELS] = {"bench/FlightHelmet/FlightHelmet.ktx.gltf"};
#else
enum { MODELS = 2 };
static const char *PATHS[MODELS] = {"bench/Sponza/Sponza.gltf", "bench/FlightHelmet/FlightHelmet.gltf"};
static const char *KTX_PATHS[MODELS] = {"bench/Sponza/Sponza.ktx.gltf", "bench/FlightHelmet/FlightHelmet.ktx.gltf"};
#endif
#ifndef LOADBENCH_KTX
#define LOADBENCH_KTX 0
#endif

#define SHOW_FRAMES 30 /* frames drawing the loaded models */

static struct {
    int phase; /* 0 = background, 1 = sync, 2 = done */
    bool loading, failed;
    int showing; /* frames left drawing the loaded models */
    double show_worst, show_first[3]; /* the first frames after they're set */
    sk_handle_t meshes[MODELS];
    sk_handle_t models[MODELS];
    sk_handle_t scene, camera;
    char paths[MODELS][512];
    double started, last, worst, last_show;
    int frames;
} b;

static void on_file(const char *path, void *user)
{
    const int i = (int)(intptr_t)user;
    snprintf(b.paths[i], sizeof(b.paths[i]), "%s", path);
}

static void on_done(const char *path, void *user)
{
    (void)path;
    (void)user;
    for (int i = 0; i < MODELS; i++) b.meshes[i] = sk_mesh_create(b.paths[i]);
    b.loading = false;
}

static void on_failed(const char *path, void *user)
{
    (void)path;
    (void)user;
    b.failed = true;
    b.loading = false;
}

static void start(bool sync)
{
    const sk_handle_t group = sk_asset_group_create();
    for (int i = 0; i < MODELS; i++) {
        const char *env = getenv("SK_LOADBENCH_KTX");
        const bool ktx = env != NULL ? env[0] == '1' : LOADBENCH_KTX;
        const char *path = ktx ? KTX_PATHS[i] : PATHS[i];
        const sk_handle_t task = sk_asset_ensure_async(path, NULL, sync ? SK_ASSET_FILE_ONLY : SK_ASSET_NONE);
        sk_asset_add_task(task, on_file, NULL, (void *)(intptr_t)i);
        sk_asset_group_add(group, task);
    }
    sk_asset_add_task(group, on_done, on_failed, NULL);
    b.loading = true;
    b.started = b.last = sk_get_time();
    b.worst = 0.0;
    b.frames = 1; /* the next frame's duration counts */
}

static void init(void *user)
{
    (void)user;
    sk_asset_set_host(ASSET_BASE);
    sk_set_target_fps(0);
    b.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(b.camera, 0.0f, 1.0f, 3.0f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f);
    b.scene = sk_scene_create();
    sk_scene_set_active_camera(b.scene, b.camera);
    /* scenes start unlit: a sun and some ambient, so the models show (and draw with
       the lit shaders a game would use) */
    const sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
    sk_light_set_intensity(sun, 3.0f);
    sk_scene_add(b.scene, sun, 0);
    sk_scene_set_ambient(b.scene, SK_COLOR_WHITE, 0.3f);
    for (int i = 0; i < MODELS; i++) {
        b.models[i] = sk_model_create(0);
        sk_scene_add(b.scene, b.models[i], 0);
    }
    start(false);
}

static void frame(float dt, float fraction, void *user)
{
    const double now = sk_get_time();
    (void)dt;
    (void)fraction;
    (void)user;
    if (now - b.last > b.worst) b.worst = now - b.last;
    b.last = now;
    b.frames++;
    sk_render_begin();
    sk_scene_draw(b.scene);
    sk_render_end();
    if (b.showing > 0) { /* the frame that just ended drew them */
        const double took = now - b.last_show;
        if (SHOW_FRAMES - b.showing < 3) b.show_first[SHOW_FRAMES - b.showing] = took;
        if (took > b.show_worst) b.show_worst = took;
        b.last_show = now;
        if (--b.showing > 0) return;
        printf("loadbench: %-10s drawing them: first frames %.1f, %.1f, %.1f ms, worst of %d %.1f ms\n",
               b.phase == 0 ? "background" : "sync", b.show_first[0] * 1000.0, b.show_first[1] * 1000.0,
               b.show_first[2] * 1000.0, SHOW_FRAMES, b.show_worst * 1000.0);
        fflush(stdout);
        for (int i = 0; i < MODELS; i++) {
            sk_model_set_mesh(b.models[i], 0);
            sk_mesh_release(b.meshes[i]);
        }
        if (++b.phase == 1) {
            start(true);
        } else {
            b.phase = 2;
            sk_request_quit();
        }
        return;
    }
    if (b.loading || b.phase == 2) return;
    if (b.failed) {
        printf("loadbench: loading failed; run tools/bench/fetch_assets.sh first\n");
        b.phase = 2;
        sk_request_quit();
        return;
    }
    printf("loadbench: %-10s worst frame %7.1f ms, loaded in %6.2f s over %d frames\n",
           b.phase == 0 ? "background" : "sync", b.worst * 1000.0, now - b.started, b.frames);
    fflush(stdout);
    for (int i = 0; i < MODELS; i++) sk_model_set_mesh(b.models[i], b.meshes[i]);
    b.showing = SHOW_FRAMES;
    b.show_worst = 0.0;
    b.last_show = now;
}

int main(void)
{
    sk_init_values(640, 360, "libsk loadbench", 0);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
