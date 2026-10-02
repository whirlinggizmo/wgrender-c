/* libwgrender loading example — loading during gameplay without stalling frames.
 *
 * Creates two environments (~330 ms of CPU work each), two models and two textures,
 * while a cube spins and a graph shows every frame's duration. Resources load on create: each comes back PENDING at once and is READY
 * or FAILED a few frames later, so this program creates them all, uses them at once,
 * and only reads their statuses, for the progress bar and a row per file. Nothing is
 * called back, and nothing waits.
 *
 *   A    load spread out: files are decoded on worker threads, and the GPU uploads
 *        are given a few milliseconds a frame (wgr_asset_set_upload_budget, 4 ms)
 *   S    load at once: no upload budget, so everything decoded is uploaded in the
 *        same frame, which the graph shows as a spike
 *   F    create a texture whose file isn't there: FAILED a frame or so later, drawn
 *        as the placeholder (the log says why)
 *   U    unload
 *   ESC  quit
 *
 * Starts with a spread-out load. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "shared/example_assets.h"
#include "wgr.h"

enum { ENVIRONMENTS = 2, MESHES = 2, TEXTURES = 2, FILES = ENVIRONMENTS + MESHES + TEXTURES + 1, GRAPH = 300 };
enum { MISSING = FILES - 1 }; /* created only on F */

static const char *PATHS[FILES] = {
    "environments/venice_sunset_1k.hdr",
    "environments/studio_small_09_1k.hdr",
    CHARACTER_PATH,
    "models/sphere/sphere.glb",
    "textures/tiles_normal.png",
    "sprites/logo/wg-logo-white-alpha.png",
    "textures/not_there.png", /* missing on purpose */
};

static struct {
    wgr_handle_t scene, camera;
    wgr_color_t bg, bar, graph_ok, graph_slow, line, cube;
    wgr_handle_t character, sphere, material;
    wgr_handle_t resources[FILES];
    bool loading, at_once;
    double load_started, load_seconds;
    double last_time;
    float frame_ms[GRAPH];
    int frame_next;
    float time;
} g;

static void release_all(void)
{
    wgr_scene_set_environment(g.scene, 0, 1.0f, 0.0f);
    wgr_scene_set_background(g.scene, 0, 0.0f);
    wgr_model_set_mesh(g.character, 0);
    wgr_model_set_mesh(g.sphere, 0);
    wgr_material_set_texture(g.material, "normal_texture", 0);
    wgr_material_set_texture(g.material, "base_color_texture", 0);
    for (int i = 0; i < FILES; i++) {
        wgr_resource_release(g.resources[i]); /* one call for every kind; a no-op for 0 */
        g.resources[i] = 0;
    }
    g.loading = false;
}

/* Create every resource and use it at once: each shows up when it's READY. */
static void start_load(bool at_once)
{
    release_all();
    g.at_once = at_once;
    wgr_asset_set_upload_budget(at_once ? 1000.0f : 4.0f); /* 4 ms is the default */
    g.load_started = wgr_get_time();
    g.loading = true;
    for (int i = 0; i < GRAPH; i++) g.frame_ms[i] = 0.0f; /* "worst" covers this load */

    for (int i = 0; i < ENVIRONMENTS; i++) g.resources[i] = wgr_environment_create(PATHS[i]);
    for (int i = ENVIRONMENTS; i < ENVIRONMENTS + MESHES; i++) g.resources[i] = wgr_mesh_create(PATHS[i]);
    for (int i = ENVIRONMENTS + MESHES; i < MISSING; i++) g.resources[i] = wgr_texture_create(PATHS[i]);

    wgr_scene_set_environment(g.scene, g.resources[0], 1.0f, 0.0f);
    wgr_scene_set_background(g.scene, g.resources[0], 0.3f);
    wgr_model_set_mesh(g.character, g.resources[2]);
    wgr_model_set_mesh(g.sphere, g.resources[3]);
    wgr_material_set_texture(g.material, "normal_texture", g.resources[4]);
}

/* How many of the files are done (READY or FAILED, or not asked for); the load is over
 * when all are. */
static int files_done(void)
{
    int done = 0;
    for (int i = 0; i < FILES; i++) done += wgr_resource_get_status(g.resources[i]) != WGR_RESOURCE_PENDING;
    return done;
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    g.bg = wgr_color_rgba(20, 22, 28, 255);
    g.bar = wgr_color_rgba(0, 0, 0, 170);
    g.graph_ok = wgr_color_rgba(90, 200, 120, 255);
    g.graph_slow = wgr_color_rgba(235, 80, 70, 255);
    g.line = wgr_color_rgba(255, 255, 255, 90);
    g.cube = wgr_color_rgba(230, 180, 60, 255);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(g.camera, 0, 1.0f, 5.5f, 0, 0.6f, 0, 0, 1, 0);
    wgr_camera3d_set_active(g.camera);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);

    g.character = wgr_model_create(0);
    wgr_model_set_transform(g.character, -1.2f, 0, 0, 0, 0.4f, 0, 0.5f, 0.5f, 0.5f);
    wgr_model_set_animation(g.character, 3);
    wgr_scene_add(g.scene, g.character, 0);

    g.sphere = wgr_model_create(0);
    wgr_model_set_transform(g.sphere, 1.2f, 0.8f, 0, 0, 0, 0, 0.8f, 0.8f, 0.8f);
    g.material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_material_set_vec4(g.material, "base_color", 0.9f, 0.9f, 0.9f, 1.0f);
    wgr_material_set_float(g.material, "roughness", 0.25f);
    wgr_model_set_material(g.sphere, 0, g.material);
    wgr_scene_add(g.scene, g.sphere, 0);

    g.last_time = wgr_get_time();
    start_load(false);
}

static void draw_graph(int x, int y, int width, int height)
{
    const float max_ms = 100.0f;
    const float bar = (float)width / GRAPH;
    float worst = 0.0f;

    wgr_shape2d_draw_rectangle(x, y, width, height, g.bar);
    for (int i = 0; i < GRAPH; i++) {
        const float ms = g.frame_ms[(g.frame_next + i) % GRAPH];
        const int h = (int)(height * (ms < max_ms ? ms : max_ms) / max_ms);
        if (h > 0) {
            wgr_shape2d_draw_rectangle(x + (int)(i * bar), y + height - h, bar > 1.0f ? (int)bar : 1, h,
                                    ms > 34.0f ? g.graph_slow : g.graph_ok);
        }
        worst = ms > worst ? ms : worst;
    }
    const int line_y = y + height - (int)(height * 16.7f / max_ms); /* a 60 Hz frame */
    wgr_shape2d_draw_line(x, line_y, x + width, line_y, g.line);

    char text[96];
    snprintf(text, sizeof(text), "frame times, 0-100 ms (line: 16.7 ms)   worst: %.0f ms", worst);
    wgr_text_draw(text, x + 6, y + 6, 10, WGR_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const wgr_keyboard_state_t kb = wgr_input_get_keyboard_state();
    const vec2_t screen = wgr_window_get_screen_size();
    const double now = wgr_get_time();
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    g.frame_ms[g.frame_next] = (float)((now - g.last_time) * 1000.0); /* real time, uncapped */
    g.frame_next = (g.frame_next + 1) % GRAPH;
    g.last_time = now;

#ifndef __EMSCRIPTEN__ /* on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to */
    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();
#endif
    if (kb.keys[WGR_KEY_A] == WGR_BUTTON_PRESSED) start_load(false);
    if (kb.keys[WGR_KEY_S] == WGR_BUTTON_PRESSED) start_load(true);
    if (kb.keys[WGR_KEY_U] == WGR_BUTTON_PRESSED) release_all();
    if (kb.keys[WGR_KEY_F] == WGR_BUTTON_PRESSED && g.resources[MISSING] == 0) {
        g.resources[MISSING] = wgr_texture_create(PATHS[MISSING]);
        wgr_material_set_texture(g.material, "base_color_texture", g.resources[MISSING]); /* the placeholder, once FAILED */
    }
    if (g.loading && files_done() == FILES) {
        g.loading = false;
        g.load_seconds = now - g.load_started;
    }

    g.time += dt;
    wgr_model_animate(g.character, dt);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_cube_wires(0, 1.9f + 0.1f * sinf(g.time * 3.0f), 0, 0.5f, 0.5f, 0.5f, g.cube);
    wgr_render_end_mode_3d();

    wgr_shape2d_draw_rectangle(0, 0, (int)screen.x, 64, g.bar);
    wgr_text_draw("libwgrender loading   A: spread out   S: all at once   F: a missing file   U: unload", 12, 12, 12,
                 WGR_COLOR_RAYWHITE);
    /* "in the background" means worker threads, and a web build only has them on a
       cross-origin-isolated page. Without them the decode lands on this thread and the
       graph below says so, so the example had better not claim otherwise. */
    snprintf(line, sizeof(line), "%s  ·  decoding on %s", wgr_get_renderer(),
             wgr_has_threads() ? "worker threads" : "the main thread (no threads in this build/host)");
    wgr_text_draw(line, 12, 26, 12, wgr_has_threads() ? WGR_COLOR_LIGHTGRAY : WGR_COLOR_GOLD);
    if (g.loading) {
        const float progress = (float)files_done() / FILES;
        snprintf(line, sizeof(line), "loading (%s)... %.0f%%", g.at_once ? "all at once" : "spread out",
                 progress * 100.0f);
        wgr_shape2d_draw_rectangle(12, 54, (int)(240 * progress), 12, g.graph_ok);
        wgr_shape2d_draw_rectangle_lines(12, 54, 240, 12, g.line);
        wgr_text_draw(line, 264, 54, 12, WGR_COLOR_LIGHTGRAY);
    } else if (g.resources[0] != 0) {
        snprintf(line, sizeof(line), "loaded %s in %.2f s", g.at_once ? "all at once" : "spread out", g.load_seconds);
        wgr_text_draw(line, 12, 54, 12, WGR_COLOR_LIGHTGRAY);
    }
    for (int i = 0; i < FILES; i++) { /* a row per file asked for: where it stands */
        if (g.resources[i] == 0) continue;
        static const char *const STATUS[] = {"", "pending", "ready", "FAILED"};
        const wgr_resource_status_t status = wgr_resource_get_status(g.resources[i]);
        const char *slash = strrchr(PATHS[i], '/');
        snprintf(line, sizeof(line), "%-8s %s", STATUS[status], slash != NULL ? slash + 1 : PATHS[i]);
        wgr_text_draw(line, 12, 80 + i * 16, 12,
                      status == WGR_RESOURCE_READY ? WGR_COLOR_LIME
                      : status == WGR_RESOURCE_FAILED ? WGR_COLOR_RED : WGR_COLOR_LIGHTGRAY);
    }
    draw_graph(12, (int)screen.y - 132, (int)screen.x - 24, 120);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1100, 720, "libwgrender loading", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
