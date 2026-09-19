/* Loading benchmark (docs/PLAN-pipeline.md): loads Sponza and FlightHelmet during
 * a running frame loop, first through the asset pipeline (background), then
 * synchronously (files only ensured, meshes created in one callback), and prints
 * the worst frame and total time of each. Needs tools/bench/fetch_assets.sh.
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

static struct {
    int phase; /* 0 = background, 1 = sync, 2 = done */
    bool loading, failed;
    sk_handle_t meshes[MODELS];
    char paths[MODELS][512];
    double started, last, worst;
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
    sk_render_end();
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
    for (int i = 0; i < MODELS; i++) sk_mesh_release(b.meshes[i]);
    if (++b.phase == 1) {
        start(true);
    } else {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(640, 360, "libsk loadbench", 0);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
