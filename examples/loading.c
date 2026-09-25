/* libwgrender loading example — loading during gameplay without stalling frames.
 *
 * Loads two environments (~330 ms of CPU work each), two models and textures, as
 * one asset group, while a cube spins and a graph shows every frame's duration.
 *
 *   A    load in the background: files are decoded on worker threads and uploaded
 *        a few milliseconds per frame, so creating them in the callback is cheap
 *   S    load synchronously for comparison: the files are only fetched
 *        (WGR_ASSET_FILE_ONLY) and created in the group's callback, in one frame
 *   U    unload
 *   ESC  quit
 *
 * Starts with a background load. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

enum { ENVIRONMENTS = 2, MESHES = 2, TEXTURES = 2, FILES = ENVIRONMENTS + MESHES + TEXTURES, GRAPH = 300 };

static const char *PATHS[FILES] = {
    "environments/venice_sunset_1k.hdr",
    "environments/studio_small_09_1k.hdr",
    "models/woman_casual/woman_casual.glb",
    "models/sphere/sphere.glb",
    "textures/tiles_normal.png",
    "sprites/logo/wg-logo-white-alpha.png",
};

static struct {
    wgr_handle_t scene, camera;
    wgr_color_t bg, bar, graph_ok, graph_slow, line, cube;
    wgr_handle_t woman_casual, sphere, material;
    wgr_handle_t group;
    bool sync;                 /* the load in progress creates everything in its group callback */
    char paths[FILES][512];    /* local paths, from the members' callbacks */
    wgr_handle_t resources[FILES];
    bool loaded;
    double load_started, load_seconds, create_ms;
    double last_time;
    float frame_ms[GRAPH];
    int frame_next;
    float time;
} g;

static void release_all(void)
{
    wgr_scene_set_environment(g.scene, 0, 1.0f, 0.0f);
    wgr_scene_set_background(g.scene, 0, 0.0f);
    wgr_model_set_mesh(g.woman_casual, 0);
    wgr_model_set_mesh(g.sphere, 0);
    wgr_material_set_texture(g.material, "normal_texture", 0);
    for (int i = 0; i < FILES; i++) {
        if (g.resources[i] == 0) continue;
        if (i < ENVIRONMENTS) wgr_environment_release(g.resources[i]);
        else if (i < ENVIRONMENTS + MESHES) wgr_mesh_release(g.resources[i]);
        else wgr_texture_release(g.resources[i]);
        g.resources[i] = 0;
    }
    g.loaded = false;
}

/* Create every resource from its local path and use them. In a background load
 * each create finds the resource the pipeline already prepared. */
static void create_all(void)
{
    const double start = wgr_get_time();
    for (int i = 0; i < ENVIRONMENTS; i++) g.resources[i] = wgr_environment_create(g.paths[i]);
    for (int i = ENVIRONMENTS; i < ENVIRONMENTS + MESHES; i++) g.resources[i] = wgr_mesh_create(g.paths[i]);
    for (int i = ENVIRONMENTS + MESHES; i < FILES; i++) g.resources[i] = wgr_texture_create(g.paths[i]);
    g.create_ms = (wgr_get_time() - start) * 1000.0;

    wgr_scene_set_environment(g.scene, g.resources[0], 1.0f, 0.0f);
    wgr_scene_set_background(g.scene, g.resources[0], 0.3f);
    wgr_model_set_mesh(g.woman_casual, g.resources[2]);
    wgr_model_set_mesh(g.sphere, g.resources[3]);
    wgr_material_set_texture(g.material, "normal_texture", g.resources[4]);
    g.loaded = true;
}

static void on_file(const char *path, void *user)
{
    snprintf(g.paths[(intptr_t)user], sizeof(g.paths[0]), "%s", path);
}

static void on_group_done(const char *path, void *user)
{
    (void)path;
    (void)user;
    g.group = 0;
    create_all();
    g.load_seconds = wgr_get_time() - g.load_started;
}

static void on_group_failed(const char *path, void *user)
{
    (void)path;
    (void)user;
    g.group = 0;
    wgr_logger_error("loading: some files failed");
}

static void start_load(bool sync)
{
    if (g.group != 0) return; /* one load at a time */
    release_all();
    g.sync = sync;
    g.load_started = wgr_get_time();
    for (int i = 0; i < GRAPH; i++) g.frame_ms[i] = 0.0f; /* "worst" covers this load */
    g.create_ms = 0.0;
    g.group = wgr_asset_group_create();
    for (int i = 0; i < FILES; i++) {
        const wgr_handle_t task = wgr_asset_ensure_async(PATHS[i], NULL, sync ? WGR_ASSET_FILE_ONLY : WGR_ASSET_NONE);
        wgr_asset_add_task(task, on_file, NULL, (void *)(intptr_t)i);
        wgr_asset_group_add(g.group, task);
    }
    wgr_asset_add_task(g.group, on_group_done, on_group_failed, NULL);
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
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

    g.woman_casual = wgr_model_create(0);
    wgr_model_set_transform(g.woman_casual, -1.2f, 0, 0, 0, 0.4f, 0, 0.5f, 0.5f, 0.5f);
    wgr_model_set_animation(g.woman_casual, 3);
    wgr_scene_add(g.scene, g.woman_casual, 0);

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

    if (kb.keys[WGR_KEY_ESCAPE] == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (kb.keys[WGR_KEY_A] == WGR_BUTTON_PRESSED) start_load(false);
    if (kb.keys[WGR_KEY_S] == WGR_BUTTON_PRESSED) start_load(true);
    if (kb.keys[WGR_KEY_U] == WGR_BUTTON_PRESSED && g.group == 0) release_all();

    g.time += dt;
    wgr_model_animate(g.woman_casual, dt);

    wgr_render_begin_frame();
    wgr_render_clear_background(g.bg);
    wgr_scene_draw(g.scene);
    wgr_render_begin_mode_3d();
    wgr_shape3d_draw_cube_wires(0, 1.9f + 0.1f * sinf(g.time * 3.0f), 0, 0.5f, 0.5f, 0.5f, g.cube);
    wgr_render_end_mode_3d();

    wgr_shape2d_draw_rectangle(0, 0, (int)screen.x, 64, g.bar);
    wgr_text_draw("libwgrender loading   A: in the background   S: synchronously   U: unload", 12, 12, 12,
                 WGR_COLOR_RAYWHITE);
    /* "in the background" means worker threads, and a web build only has them on a
       cross-origin-isolated page. Without them the decode lands on this thread and the
       graph below says so, so the example had better not claim otherwise. */
    snprintf(line, sizeof(line), "%s  ·  decoding on %s", wgr_get_renderer(),
             wgr_has_threads() ? "worker threads" : "the main thread (no threads in this build/host)");
    wgr_text_draw(line, 12, 26, 12, wgr_has_threads() ? WGR_COLOR_LIGHTGRAY : WGR_COLOR_GOLD);
    if (g.group != 0) {
        const float progress = wgr_asset_get_progress(g.group);
        snprintf(line, sizeof(line), "loading (%s)... %.0f%%",
                 g.sync ? "synchronously" : (wgr_has_threads() ? "in the background" : "in the background, but on this thread"),
                 progress * 100.0f);
        wgr_shape2d_draw_rectangle(12, 54, (int)(240 * progress), 12, g.graph_ok);
        wgr_shape2d_draw_rectangle_lines(12, 54, 240, 12, g.line);
        wgr_text_draw(line, 264, 54, 12, WGR_COLOR_LIGHTGRAY);
    } else if (g.loaded) {
        snprintf(line, sizeof(line), "loaded %d files %s in %.2f s; creating them took %.0f ms",
                 FILES, g.sync ? "synchronously" : (wgr_has_threads() ? "in the background" : "without threads"),
                 g.load_seconds, g.create_ms);
        wgr_text_draw(line, 12, 54, 12, WGR_COLOR_LIGHTGRAY);
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
