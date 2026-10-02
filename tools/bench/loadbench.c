/* Loading benchmark (docs/HISTORY.md, "Loading pipeline (background preparation, budgeted GPU upload)"): loads Sponza and FlightHelmet during
 * a running frame loop, on create, first with the GPU uploads given a budget a frame
 * (wgr_asset_set_upload_budget, 4 ms: the default), then with none (everything
 * prepared is uploaded in the frame it's ready), and prints the worst frame and total
 * time of each, then the worst of the first frames that draw the loaded models. A
 * warm-up load of the same models comes first, drawn and released, unmeasured: it takes
 * the one-time costs (files from disk, shaders and pipelines made, the driver's first
 * uploads), so neither measured case pays them and their order doesn't matter. The
 * files are decoded on worker threads either way. Needs tools/bench/fetch_bench_models.py (run.py runs it).
 *
 *   tools/bench/run_benchmark.py loadbench                   headless: CPU work only (no GPU uploads)
 *   tools/bench/run_benchmark.py loadbench --desktop         desktop: real GL uploads (opens a window)
 *   tools/bench/run_benchmark.py loadbench --desktop --ktx   the models with compressed textures
 *                             (tools/compress_textures.py --gltf, made the first time;
 *                             WGR_LOADBENCH_KTX=1 loads name.ktx.gltf) */
#include <stdio.h>
#include <stdlib.h>

#include "wgr.h"

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
    int phase; /* -1 = warming up, 0 = with an upload budget, 1 = with none, 2 = done */
    bool loading, failed;
    int showing; /* frames left drawing the loaded models */
    double show_worst, show_first[3]; /* the first frames after they're set */
    wgr_handle_t meshes[MODELS];
    wgr_handle_t models[MODELS];
    wgr_handle_t scene, camera;
    double started, last, worst, last_show;
    int frames;
} b;

static const char *phase_name(void)
{
    return b.phase < 0 ? "warm-up" : b.phase == 0 ? "budget" : "at once";
}

static void start(bool budget)
{
    const char *env = getenv("WGR_LOADBENCH_KTX");
    const bool ktx = env != NULL ? env[0] == '1' : LOADBENCH_KTX;
    wgr_asset_set_upload_budget(budget ? 4.0f : 1000.0f);
    for (int i = 0; i < MODELS; i++) {
        b.meshes[i] = wgr_mesh_create(ktx ? KTX_PATHS[i] : PATHS[i]); /* loads on create */
    }
    b.loading = true;
    b.started = b.last = wgr_get_time();
    b.worst = 0.0;
    b.frames = 1; /* the next frame's duration counts */
}

static void init(void *user)
{
    (void)user;
    wgr_asset_set_host(ASSET_BASE);
    wgr_set_target_fps(0);
    b.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(b.camera, 0.0f, 1.0f, 3.0f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f);
    b.scene = wgr_scene_create();
    wgr_scene_set_active_camera(b.scene, b.camera);
    /* scenes start unlit: a sun and some ambient, so the models show (and draw with
       the lit shaders a game would use) */
    const wgr_handle_t sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(sun, -0.6f, -1.0f, -0.5f);
    wgr_light_set_intensity(sun, 3.0f);
    wgr_scene_add(b.scene, sun, 0);
    wgr_scene_set_ambient(b.scene, WGR_COLOR_WHITE, 0.3f);
    for (int i = 0; i < MODELS; i++) {
        b.models[i] = wgr_model_create(0);
        wgr_scene_add(b.scene, b.models[i], 0);
    }
    b.phase = -1;
    start(true);
}

/* Whether both meshes have loaded; a failed one ends the run. */
static bool loaded(void)
{
    bool all = true;
    for (int i = 0; i < MODELS; i++) {
        const wgr_resource_status_t status = wgr_resource_get_status(b.meshes[i]);
        b.failed = b.failed || status == WGR_RESOURCE_FAILED;
        all = all && status == WGR_RESOURCE_READY;
    }
    return all;
}

static void frame(float dt, float fraction, void *user)
{
    const double now = wgr_get_time();
    (void)dt;
    (void)fraction;
    (void)user;
    if (now - b.last > b.worst) b.worst = now - b.last;
    b.last = now;
    b.frames++;
    wgr_render_begin_frame();
    wgr_scene_draw(b.scene);
    wgr_render_end_frame();
    if (b.showing > 0) { /* the frame that just ended drew them */
        const double took = now - b.last_show;
        if (SHOW_FRAMES - b.showing < 3) b.show_first[SHOW_FRAMES - b.showing] = took;
        if (took > b.show_worst) b.show_worst = took;
        b.last_show = now;
        if (--b.showing > 0) return;
        if (b.phase >= 0) {
            printf("loadbench: %-10s drawing them: first frames %.1f, %.1f, %.1f ms, worst of %d %.1f ms\n",
                   phase_name(), b.show_first[0] * 1000.0, b.show_first[1] * 1000.0, b.show_first[2] * 1000.0,
                   SHOW_FRAMES, b.show_worst * 1000.0);
            fflush(stdout);
        }
        for (int i = 0; i < MODELS; i++) {
            wgr_model_set_mesh(b.models[i], 0);
            wgr_resource_release(b.meshes[i]);
        }
        if (++b.phase <= 1) {
            start(b.phase == 0); /* after the warm-up the budget, then none */
        } else {
            b.phase = 2;
            wgr_request_quit();
        }
        return;
    }
    if (b.phase == 2 || (b.loading && !loaded() && !b.failed)) return;
    b.loading = false;
    if (b.failed) {
        printf("loadbench: loading failed; run tools/bench/fetch_bench_models.py first\n");
        b.phase = 2;
        wgr_request_quit();
        return;
    }
    printf("loadbench: %-10s worst frame %7.1f ms, loaded in %6.2f s over %d frames%s\n", phase_name(),
           b.worst * 1000.0, now - b.started, b.frames, b.phase < 0 ? " (not measured)" : "");
    fflush(stdout);
    for (int i = 0; i < MODELS; i++) wgr_model_set_mesh(b.models[i], b.meshes[i]);
    b.showing = SHOW_FRAMES;
    b.show_worst = 0.0;
    b.last_show = now;
}

int main(void)
{
    wgr_init_values(640, 360, "libwgrender loadbench", 0);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
