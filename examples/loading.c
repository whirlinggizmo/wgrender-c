/* libsk loading example — loading during gameplay without stalling frames.
 *
 * Loads two environments (~330 ms of CPU work each), two models and textures, as
 * one asset group, while a cube spins and a graph shows every frame's duration.
 *
 *   A    load in the background: files are decoded on worker threads and uploaded
 *        a few milliseconds per frame, so creating them in the callback is cheap
 *   S    load synchronously for comparison: the files are only fetched
 *        (SK_ASSET_FILE_ONLY) and created in the group's callback, in one frame
 *   U    unload
 *   ESC  quit
 *
 * Starts with a background load. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

enum { ENVIRONMENTS = 2, MESHES = 2, TEXTURES = 2, FILES = ENVIRONMENTS + MESHES + TEXTURES, GRAPH = 300 };

static const char *PATHS[FILES] = {
    "environments/venice_sunset_1k.hdr",
    "environments/studio_small_09_1k.hdr",
    "models/gumshoe/gumshoe.glb",
    "models/sphere/sphere.glb",
    "textures/tiles_normal.png",
    "sprites/logo/wg-logo-white-alpha.png",
};

static struct {
    sk_handle_t scene, camera, bg, bar, graph_ok, graph_slow, line, cube;
    sk_handle_t gumshoe, sphere, material;
    sk_handle_t group;
    bool sync;                 /* the load in progress creates everything in its group callback */
    char paths[FILES][512];    /* local paths, from the members' callbacks */
    sk_handle_t resources[FILES];
    bool loaded;
    double load_started, load_seconds, create_ms;
    double last_time;
    float frame_ms[GRAPH];
    int frame_next;
    float time;
} g;

static void release_all(void)
{
    sk_scene_set_environment(g.scene, 0, 1.0f, 0.0f);
    sk_scene_set_background(g.scene, 0, 0.0f);
    sk_model_set_mesh(g.gumshoe, 0);
    sk_model_set_mesh(g.sphere, 0);
    sk_material_set_texture(g.material, "normal_texture", 0);
    for (int i = 0; i < FILES; i++) {
        if (g.resources[i] == 0) continue;
        if (i < ENVIRONMENTS) sk_environment_destroy(g.resources[i]);
        else if (i < ENVIRONMENTS + MESHES) sk_mesh_destroy(g.resources[i]);
        else sk_texture_destroy(g.resources[i]);
        g.resources[i] = 0;
    }
    g.loaded = false;
}

/* Create every resource from its local path and use them. In a background load
 * each create finds the resource the pipeline already prepared. */
static void create_all(void)
{
    const double start = sk_get_time();
    for (int i = 0; i < ENVIRONMENTS; i++) g.resources[i] = sk_environment_create(g.paths[i]);
    for (int i = ENVIRONMENTS; i < ENVIRONMENTS + MESHES; i++) g.resources[i] = sk_mesh_create(g.paths[i]);
    for (int i = ENVIRONMENTS + MESHES; i < FILES; i++) g.resources[i] = sk_texture_create(g.paths[i]);
    g.create_ms = (sk_get_time() - start) * 1000.0;

    sk_scene_set_environment(g.scene, g.resources[0], 1.0f, 0.0f);
    sk_scene_set_background(g.scene, g.resources[0], 0.3f);
    sk_model_set_mesh(g.gumshoe, g.resources[2]);
    sk_model_set_mesh(g.sphere, g.resources[3]);
    sk_material_set_texture(g.material, "normal_texture", g.resources[4]);
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
    g.load_seconds = sk_get_time() - g.load_started;
}

static void on_group_failed(const char *path, void *user)
{
    (void)path;
    (void)user;
    g.group = 0;
    sk_logger_error("loading: some files failed");
}

static void start_load(bool sync)
{
    if (g.group != 0) return; /* one load at a time */
    release_all();
    g.sync = sync;
    g.load_started = sk_get_time();
    for (int i = 0; i < GRAPH; i++) g.frame_ms[i] = 0.0f; /* "worst" covers this load */
    g.create_ms = 0.0;
    g.group = sk_asset_group_create();
    for (int i = 0; i < FILES; i++) {
        const sk_handle_t task = sk_asset_ensure_async(PATHS[i], NULL, sync ? SK_ASSET_FILE_ONLY : SK_ASSET_NONE);
        sk_asset_add_task(task, on_file, NULL, (void *)(intptr_t)i);
        sk_asset_group_add(g.group, task);
    }
    sk_asset_add_task(g.group, on_group_done, on_group_failed, NULL);
}

static void init(void *user_data)
{
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g.bg = sk_color_create(20, 22, 28, 255);
    g.bar = sk_color_create(0, 0, 0, 170);
    g.graph_ok = sk_color_create(90, 200, 120, 255);
    g.graph_slow = sk_color_create(235, 80, 70, 255);
    g.line = sk_color_create(255, 255, 255, 90);
    g.cube = sk_color_create(230, 180, 60, 255);

    g.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(g.camera, 0, 1.0f, 5.5f, 0, 0.6f, 0, 0, 1, 0);
    sk_camera3d_set_active(g.camera);
    g.scene = sk_scene_create();
    sk_scene_set_active_camera(g.scene, g.camera);

    g.gumshoe = sk_model_create(0);
    sk_model_set_transform(g.gumshoe, -1.2f, 0, 0, 0, 0.4f, 0, 0.5f, 0.5f, 0.5f);
    sk_model_set_animation(g.gumshoe, 3);
    sk_scene_add(g.scene, g.gumshoe, 0);

    g.sphere = sk_model_create(0);
    sk_model_set_transform(g.sphere, 1.2f, 0.8f, 0, 0, 0, 0, 0.8f, 0.8f, 0.8f);
    g.material = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_vec4(g.material, "base_color", 0.9f, 0.9f, 0.9f, 1.0f);
    sk_material_set_float(g.material, "roughness", 0.25f);
    sk_model_set_material(g.sphere, 0, g.material);
    sk_scene_add(g.scene, g.sphere, 0);

    g.last_time = sk_get_time();
    start_load(false);
}

static void draw_graph(int x, int y, int width, int height)
{
    const float max_ms = 100.0f;
    const float bar = (float)width / GRAPH;
    float worst = 0.0f;

    sk_shape2d_draw_rectangle(x, y, width, height, g.bar);
    for (int i = 0; i < GRAPH; i++) {
        const float ms = g.frame_ms[(g.frame_next + i) % GRAPH];
        const int h = (int)(height * (ms < max_ms ? ms : max_ms) / max_ms);
        if (h > 0) {
            sk_shape2d_draw_rectangle(x + (int)(i * bar), y + height - h, bar > 1.0f ? (int)bar : 1, h,
                                    ms > 34.0f ? g.graph_slow : g.graph_ok);
        }
        worst = ms > worst ? ms : worst;
    }
    const int line_y = y + height - (int)(height * 16.7f / max_ms); /* a 60 Hz frame */
    sk_shape2d_draw_line(x, line_y, x + width, line_y, g.line);

    char text[96];
    snprintf(text, sizeof(text), "frame times, 0-100 ms (line: 16.7 ms)   worst: %.0f ms", worst);
    sk_text_draw(text, x + 6, y + 6, 10, SK_COLOR_LIGHTGRAY);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const vec2_t screen = sk_window_get_screen_size();
    const double now = sk_get_time();
    char line[160];

    (void)tick_fraction;
    (void)user_data;
    g.frame_ms[g.frame_next] = (float)((now - g.last_time) * 1000.0); /* real time, uncapped */
    g.frame_next = (g.frame_next + 1) % GRAPH;
    g.last_time = now;

    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();
    if (kb.keys[SK_KEY_A] == SK_BUTTON_PRESSED) start_load(false);
    if (kb.keys[SK_KEY_S] == SK_BUTTON_PRESSED) start_load(true);
    if (kb.keys[SK_KEY_U] == SK_BUTTON_PRESSED && g.group == 0) release_all();

    g.time += dt;
    sk_model_animate(g.gumshoe, dt);

    sk_render_begin();
    sk_render_clear_background(g.bg);
    sk_scene_draw(g.scene);
    sk_render_begin_mode_3d();
    sk_shape3d_draw_cube_wires(0, 1.9f + 0.1f * sinf(g.time * 3.0f), 0, 0.5f, 0.5f, 0.5f, g.cube);
    sk_render_end_mode_3d();

    sk_shape2d_draw_rectangle(0, 0, (int)screen.x, 64, g.bar);
    sk_text_draw("libsk loading   A: in the background   S: synchronously   U: unload", 12, 12, 12,
                 SK_COLOR_RAYWHITE);
    if (g.group != 0) {
        const float progress = sk_asset_get_progress(g.group);
        snprintf(line, sizeof(line), "loading (%s)... %.0f%%", g.sync ? "synchronously" : "in the background",
                 progress * 100.0f);
        sk_shape2d_draw_rectangle(12, 40, (int)(240 * progress), 12, g.graph_ok);
        sk_shape2d_draw_rectangle_lines(12, 40, 240, 12, g.line);
        sk_text_draw(line, 264, 40, 12, SK_COLOR_LIGHTGRAY);
    } else if (g.loaded) {
        snprintf(line, sizeof(line), "loaded %d files %s in %.2f s; creating them took %.0f ms",
                 FILES, g.sync ? "synchronously" : "in the background", g.load_seconds, g.create_ms);
        sk_text_draw(line, 12, 40, 12, SK_COLOR_LIGHTGRAY);
    }
    draw_graph(12, (int)screen.y - 132, (int)screen.x - 24, 120);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1100, 720, "libsk loading", SK_WINDOW_FLAG_MSAA_4X_HINT);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
