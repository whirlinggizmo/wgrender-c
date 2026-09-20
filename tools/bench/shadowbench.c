/* Shadow benchmark: what a casting light costs a frame (docs/PLAN-shadows.md).
 *
 * The same scene each time — a floor and a grid of generated shapes, half of them
 * turning, under a perspective camera — drawn with:
 *
 *   off                no light casts: the baseline
 *   1024, 2048, 4096   the sun casts, at three map sizes. The pass over every caster
 *                      is the same work each time and its CPU cost doesn't move, so
 *                      what changes between these rows is the map's fill: GPU only
 *   two 1024           the sun and a spot cast: two passes, two maps
 *   no receive         the sun casts but nothing receives. libsk skips the pass when
 *                      no model receives and no lit sprite is in the scene, so this
 *                      should land on top of "off": it is the check that it does
 *   look away          the sun casts, but the camera faces away from the grid. Every
 *                      model is behind it, so frustum culling (docs/PLAN-culling.md)
 *                      should submit almost nothing
 *   away, no cull      the same, with the scene's culling switched off: what that
 *                      frame cost before there was any. The gap between these two
 *                      rows is what culling is worth
 *
 * at two model counts, to see what scales with casters and what doesn't. Nothing is
 * loaded from disk: the shapes are generated, so the numbers are the renderer's.
 *
 * Read the frame column against the cpu column: where they move together the cost is
 * submitting draws, and where the frame moves while cpu doesn't it is the GPU. Frame
 * time only means anything where frames aren't paced by a display:
 *
 *   make shadowbench            headless: CPU only, no GPU at all
 *   make shadowbench DESKTOP=1  desktop, vsync off: real GPU cost (opens a window)
 *   make shadowbench-web        web page (build/<backend>/bench/); results in the
 *                               browser console; frames are paced by the display, so
 *                               read the CPU columns, or run it with the frame rate
 *                               uncapped in the browser's own profiler */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "sk.h"

#define WARMUP_FRAMES 20
#define MEASURE_FRAMES 100
#define MAX_MODELS 4000

typedef enum {
    CASE_OFF,
    CASE_1024,
    CASE_2048,
    CASE_4096,
    CASE_TWO_LIGHTS,
    CASE_NO_RECEIVE,
    CASE_AWAY,
    CASE_AWAY_NO_CULL,
    CASES,
} bench_case_t;

static const char *CASE_NAMES[CASES] = {"off",        "sun 1024", "sun 2048",  "sun 4096",
                                        "two 1024",   "no receive", "look away", "away, no cull"};
enum { COUNT_STEPS = 4 };
static const int MODEL_COUNTS[COUNT_STEPS] = {100, 400, 1000, 4000};

typedef struct {
    int models;
    const char *name;
    double frame_mean, frame_worst;
    double cpu_mean, update_mean, scene_mean, submit_mean, cpu_worst;
} result_t;

static struct {
    sk_handle_t scene, camera, sun, spot;
    sk_handle_t models[MAX_MODELS];
    sk_handle_t floor;
    int model_count;
    bench_case_t which;
    int step;  /* case * COUNT_STEPS + count step */
    int frame;
    double last, frame_sum, cpu_sum, update_sum, scene_sum, submit_sum, worst, cpu_worst;
    float angle;
    result_t results[CASES * COUNT_STEPS];
} b;

/* One model of `mesh` at (x, y, z), added to the scene with a plain lit material. */
static sk_handle_t place(sk_handle_t mesh, float x, float y, float z, float r, float g, float bl)
{
    const sk_handle_t model = sk_model_create(mesh);
    const sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
    sk_mesh_release(mesh);
    sk_model_set_transform(model, x, y, z, 0, 0, 0, 1, 1, 1);
    sk_material_set_vec4(material, "base_color", r, g, bl, 1.0f);
    sk_material_set_float(material, "metallic", 0.0f);
    sk_material_set_float(material, "roughness", 0.55f);
    sk_model_set_material(model, -1, material);
    sk_material_release(material);
    sk_scene_add(b.scene, model, 0);
    return model;
}

static void setup(void)
{
    const bench_case_t which = (bench_case_t)(b.step / COUNT_STEPS);
    const int count = MODEL_COUNTS[b.step % COUNT_STEPS];
    const int side = (int)ceilf(sqrtf((float)count));
    const float spacing = 2.2f;
    result_t *r = &b.results[b.step];

    b.which = which;
    b.model_count = count;
    r->models = count;
    r->name = CASE_NAMES[which];

    b.floor = place(sk_mesh_create_plane(side * spacing * 1.6f, side * spacing * 1.6f, 0), 0, 0, 0,
                    0.4f, 0.42f, 0.45f);
    for (int i = 0; i < count; i++) {
        const float x = ((float)(i % side) - (float)side * 0.5f) * spacing;
        const float z = ((float)(i / side) - (float)side * 0.5f) * spacing;
        const float hue = (float)i / (float)count;
        /* a mix of shapes, so the depth pass sees a range of triangle counts */
        const sk_handle_t mesh = (i % 3 == 0)   ? sk_mesh_create_sphere(0.55f, 16, 32)
                                 : (i % 3 == 1) ? sk_mesh_create_cube(0.9f, 1.2f, 0.9f)
                                                : sk_mesh_create_torus(0.5f, 0.18f, 32, 16);
        b.models[i] = place(mesh, x, 0.8f, z, 0.3f + hue * 0.6f, 0.5f, 0.9f - hue * 0.6f);
    }

    /* the light casts (or doesn't) for this case */
    sk_light_set_casts_shadows(b.sun, which != CASE_OFF);
    sk_light_set_casts_shadows(b.spot, which == CASE_TWO_LIGHTS);
    sk_scene_set_culling(b.scene, which != CASE_AWAY_NO_CULL);
    sk_light_set_shadow_map_size(b.sun, which == CASE_2048 ? 2048 : which == CASE_4096 ? 4096 : 1024);
    sk_light_set_shadow_map_size(b.spot, 1024);
    for (int i = 0; i < count; i++) {
        sk_model_set_receives_shadow(b.models[i], which != CASE_NO_RECEIVE);
    }
    sk_model_set_receives_shadow(b.floor, which != CASE_NO_RECEIVE);

    b.frame = 0;
    b.frame_sum = b.cpu_sum = b.update_sum = b.scene_sum = b.submit_sum = 0.0;
    b.worst = b.cpu_worst = 0.0;
}

static void teardown(void)
{
    for (int i = 0; i < b.model_count; i++) {
        sk_scene_remove(b.scene, b.models[i]);
        sk_model_destroy(b.models[i]);
        b.models[i] = 0;
    }
    sk_scene_remove(b.scene, b.floor);
    sk_model_destroy(b.floor);
    b.floor = 0;
    b.model_count = 0;
}

/* Half the models turn, so a frame's casters aren't all identical to the last. */
static void update(float dt)
{
    b.angle += dt * 0.3f;
    for (int i = 0; i < b.model_count; i += 2) {
        const int side = (int)ceilf(sqrtf((float)b.model_count));
        const float x = ((float)(i % side) - (float)side * 0.5f) * 2.2f;
        const float z = ((float)(i / side) - (float)side * 0.5f) * 2.2f;
        sk_model_set_transform(b.models[i], x, 0.8f, z, 0, b.angle * 40.0f, b.angle * 25.0f, 1, 1, 1);
    }
    /* the camera keeps moving, so the shadow fit re-snaps like it would in a game */
    const float radius = 6.0f + (float)b.model_count * 0.06f;
    const float x = radius * sinf(b.angle * 0.5f), z = radius * cosf(b.angle * 0.5f);
    if (b.which == CASE_AWAY || b.which == CASE_AWAY_NO_CULL) {
        /* outward from the grid: every model is behind the camera */
        sk_camera3d_set_view(b.camera, x, radius * 0.45f, z, x * 2.0f, radius * 0.9f, z * 2.0f, 0, 1, 0);
        return;
    }
    sk_camera3d_set_view(b.camera, x, radius * 0.45f, z, 0, 0.8f, 0, 0, 1, 0);
}

static void print_results(void)
{
#if defined(SK_HEADLESS)
    /* without a display the runtime paces frames at a stand-in 60 Hz (src/sk.c), so
       frame-to-frame time is that interval, not a measurement: report CPU only */
    const bool frames = false;
    const char *build = "headless: CPU only, no GPU";
#elif defined(__EMSCRIPTEN__)
    const bool frames = false; /* paced by the display */
    const char *build = "web: CPU only, frames paced by the display";
#else
    const bool frames = true;
    const char *build = "desktop, vsync off";
#endif
    printf("\nshadowbench (%s)\n", build);
    printf("%-12s %7s", "case", "models");
    if (frames) printf(" %8s %8s", "frame ms", "worst ms");
    printf(" %7s %7s %7s %7s %7s\n", "cpu ms", "update", "scene", "submit", "cpu max");
    for (int i = 0; i < CASES * COUNT_STEPS; i++) {
        const result_t *r = &b.results[i];
        printf("%-12s %7d", r->name != NULL ? r->name : "?", r->models);
        if (frames) printf(" %8.2f %8.2f", r->frame_mean, r->frame_worst);
        printf(" %7.2f %7.2f %7.2f %7.2f %7.2f\n", r->cpu_mean, r->update_mean, r->scene_mean, r->submit_mean,
               r->cpu_worst);
    }
    printf("shadowbench: done\n");
    fflush(stdout);
}

static void init(void *user)
{
    (void)user;
    sk_set_target_fps(0);
    b.camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    b.scene = sk_scene_create();
    sk_scene_set_active_camera(b.scene, b.camera);
    sk_scene_set_ambient(b.scene, sk_color_rgba(140, 170, 225, 255), 0.25f);

    b.sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(b.sun, -0.6f, -1.0f, -0.35f);
    sk_light_set_intensity(b.sun, 3.0f);
    sk_light_set_shadow_distance(b.sun, 40.0f);
    sk_scene_add(b.scene, b.sun, 0);

    b.spot = sk_light_create(SK_LIGHT_SPOT);
    sk_light_set_position(b.spot, 6.0f, 9.0f, 6.0f);
    sk_light_set_direction(b.spot, -0.6f, -1.0f, -0.6f);
    sk_light_set_intensity(b.spot, 240.0f);
    sk_light_set_range(b.spot, 30.0f);
    sk_light_set_spot_cone(b.spot, 0.35f, 0.5f);
    sk_light_set_shadow_distance(b.spot, 30.0f);
    sk_scene_add(b.scene, b.spot, 0);

    setup();
    b.last = sk_get_time();
}

static void frame(float dt, float fraction, void *user)
{
    const double start = sk_get_time();
    double updated, drawn, submitted;
    result_t *r = &b.results[b.step];
    (void)fraction;
    (void)user;

    update(dt);
    updated = sk_get_time();
    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(120, 150, 200, 255));
    sk_scene_draw(b.scene);
    drawn = sk_get_time();
    sk_render_end(); /* the shadow passes happen in here, before the screen's */
    submitted = sk_get_time();

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
        if (++b.step == CASES * COUNT_STEPS) {
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
    sk_init_values(960, 600, "libsk shadowbench", SK_WINDOW_FLAG_VSYNC_OFF | SK_WINDOW_FLAG_LOW_DPI);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
