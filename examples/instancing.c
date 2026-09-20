/* libsk instancing example — many models that share a mesh and a material.
 *
 * Nothing here asks for instancing: it is what libsk does when models agree on
 * everything but where they stand (docs/PLAN-instancing.md). A field of cubes shares
 * one mesh and one material and differs only in transform and tint, so it is one draw;
 * six gumshoes share the same asset and animate out of step, so their joints are
 * per instance; a few cubes are see-through, and those keep their back-to-front order.
 * The sun casts, so the same batching happens again into its shadow map: 400 cubes and
 * six walkers go into it as two draws, and every shadow lands under its own model.
 * Press SPACE to give every cube its own material instead, which is the same picture
 * drawn one cube at a time. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "example_assets.h"
#include "sk.h"

#define MODEL_PATH "models/gumshoe/gumshoe.glb"
#define FIELD_SIDE 20
#define FIELD_COUNT (FIELD_SIDE * FIELD_SIDE)
#define WALKERS 6
#define GLASS 5

static sk_handle_t g_scene, g_camera;
static sk_handle_t g_cubes[FIELD_COUNT];
static sk_handle_t g_walkers[WALKERS];
static sk_handle_t g_glass[GLASS];
static sk_handle_t g_shared_material;
static bool g_own_materials;

static void on_mesh_loaded(const char *path, void *user)
{
    const sk_handle_t model = (sk_handle_t)(uintptr_t)user;
    const sk_handle_t mesh = sk_mesh_create(path); /* the same resource for every walker */
    sk_model_set_mesh(model, mesh);
    sk_mesh_release(mesh);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static sk_color_t field_tint(int i, unsigned char alpha)
{
    const float hue = (float)i / (float)FIELD_COUNT;
    return sk_color_rgba((int)(120 + 135 * sinf(hue * 6.28f)), (int)(120 + 135 * sinf(hue * 6.28f + 2.1f)),
                         (int)(120 + 135 * sinf(hue * 6.28f + 4.2f)), alpha);
}

/* One material for every cube, or one each: the same picture, batched or not. */
static void set_materials(bool own)
{
    for (int i = 0; i < FIELD_COUNT; i++) {
        if (own) {
            const sk_handle_t material = sk_material_create(SK_MATERIAL_PBR);
            sk_material_set_float(material, "roughness", 0.5f);
            sk_model_set_material(g_cubes[i], -1, material);
            sk_material_release(material); /* the model keeps its reference */
        } else {
            sk_model_set_material(g_cubes[i], -1, g_shared_material);
        }
    }
    g_own_materials = own;
}

static void on_init(void *user)
{
    (void)user;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    g_camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    g_scene = sk_scene_create();
    sk_scene_set_active_camera(g_scene, g_camera);

    const sk_handle_t sun = sk_light_create(SK_LIGHT_DIRECTIONAL);
    sk_light_set_direction(sun, -0.5f, -1.0f, -0.4f);
    sk_light_set_intensity(sun, 3.0f);
    sk_light_set_shadow_distance(sun, 60.0f);
    sk_light_set_casts_shadows(sun, true); /* the depth pass batches the same way */
    sk_scene_add(g_scene, sun, 0);
    sk_scene_set_ambient(g_scene, sk_color_rgba(160, 180, 220, 255), 0.35f);
    sk_debug_enable_fps(12, 10, 16);

    /* something for the shadows to land on */
    const sk_handle_t floor_mesh = sk_mesh_create_plane(60.0f, 60.0f, 0);
    const sk_handle_t floor_material = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_vec4(floor_material, "base_color", 0.45f, 0.47f, 0.5f, 1.0f);
    sk_material_set_float(floor_material, "roughness", 0.9f);
    const sk_handle_t floor = sk_model_create(floor_mesh);
    sk_model_set_material(floor, -1, floor_material);
    sk_model_set_transform(floor, 0, -0.6f, 0, 0, 0, 0, 1, 1, 1);
    sk_scene_add(g_scene, floor, 0);
    sk_mesh_release(floor_mesh);
    sk_material_release(floor_material);

    /* the field: one mesh, one material, a tint each */
    const sk_handle_t cube = sk_mesh_create_cube(0.6f, 0.6f, 0.6f);
    g_shared_material = sk_material_create(SK_MATERIAL_PBR);
    sk_material_set_float(g_shared_material, "roughness", 0.5f);
    for (int i = 0; i < FIELD_COUNT; i++) {
        g_cubes[i] = sk_model_create(cube);
        sk_model_set_tint(g_cubes[i], field_tint(i, 255));
        sk_scene_add(g_scene, g_cubes[i], 0);
    }
    set_materials(false);

    /* see-through copies of the same cube: same mesh, same material, alpha in the tint */
    for (int i = 0; i < GLASS; i++) {
        g_glass[i] = sk_model_create(cube);
        sk_model_set_material(g_glass[i], -1, g_shared_material);
        sk_model_set_tint(g_glass[i], sk_color_rgba(255, 255, 255, 110));
        sk_model_set_transform(g_glass[i], (float)i * 2.0f - 4.0f, 5.2f, 5.0f, 0, 0, 0, 2, 2, 2);
        sk_scene_add(g_scene, g_glass[i], 0);
    }
    sk_mesh_release(cube);

    /* six walkers sharing one skinned mesh, each at its own point in the walk */
    for (int i = 0; i < WALKERS; i++) {
        g_walkers[i] = sk_model_create(0);
        sk_model_set_transform(g_walkers[i], (float)i * 2.4f - 6.0f, 0.0f, -2.0f, 0, 3.14159f, 0, 1, 1, 1);
        sk_model_set_animation(g_walkers[i], 3);
        sk_model_set_animation_loop(g_walkers[i], true);
        sk_scene_add(g_scene, g_walkers[i], 0);
    }
    sk_asset_add_task(sk_asset_ensure_async(MODEL_PATH, NULL, 0), on_mesh_loaded, on_failed,
                      (void *)(uintptr_t)g_walkers[0]);
    for (int i = 1; i < WALKERS; i++) {
        sk_asset_add_task(sk_asset_ensure_async(MODEL_PATH, NULL, 0), on_mesh_loaded, on_failed,
                          (void *)(uintptr_t)g_walkers[i]);
    }
}

static void frame(float dt, float fraction, void *user)
{
    const float t = (float)sk_get_time();
    char line[96];
    (void)fraction;
    (void)user;

    sk_camera3d_set_view(g_camera, sinf(t * 0.15f) * 22.0f, 12.0f, cosf(t * 0.15f) * 22.0f, 0, 1.0f, 0, 0, 1, 0);
    for (int i = 0; i < FIELD_COUNT; i++) {
        const float x = ((float)(i % FIELD_SIDE) - FIELD_SIDE * 0.5f + 0.5f) * 1.5f;
        const float z = ((float)(i / FIELD_SIDE) - FIELD_SIDE * 0.5f + 0.5f) * 1.5f;
        const float wave = sinf(t * 1.5f + x * 0.6f + z * 0.4f);
        /* well clear of the floor, so every cube throws its own shadow onto it */
        sk_model_set_transform(g_cubes[i], x, 2.4f + wave * 0.5f, z, 0, t * 0.3f + (float)i, 0, 1, 1, 1);
    }
    for (int i = 0; i < WALKERS; i++) {
        /* the same walk, out of step: a shared mesh, but each its own pose */
        sk_model_set_animation_time(g_walkers[i], t + (float)i * 0.35f);
    }

    sk_render_begin();
    sk_render_clear_background(sk_color_rgba(28, 30, 38, 255));
    sk_scene_draw(g_scene);
    sk_text_draw("libsk instancing: models that share a mesh and a material go up as one draw", 12, 36, 20,
                 SK_COLOR_RAYWHITE);
    snprintf(line, sizeof line, "%d cubes, %s   SPACE toggles   ESC quit", FIELD_COUNT,
             g_own_materials ? "a material each (one draw each)" : "one material (one draw)");
    sk_text_draw(line, 12, 64, 16, SK_COLOR_LIGHTGRAY);
    sk_render_end();

    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    if (kb.keys[SK_KEY_SPACE] == SK_BUTTON_PRESSED) {
        set_materials(!g_own_materials);
    }
    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) {
        sk_request_quit();
    }
}

int main(void)
{
    sk_init_values(1024, 720, "libsk instancing", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(on_init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
