/* libwgrender shadows example — a casting light and what it does to a scene.
 *
 * The sun casts (wgr_light_set_casts_shadows): once a frame it draws everything that
 * casts into a depth map, and the lit shading darkens what's behind something. The
 * scene is a floor, a wall, some generated shapes and an animated woman, so the
 * shadows fall across each other and across themselves.
 *
 *   - 1 turns the sun's casting on and off, the difference this whole feature makes
 *   - 2 does the same for a spot light circling the scene, which casts through its own
 *     cone: two lights casting at once, a layer of the shadow map each
 *   - the ball on the left doesn't cast (wgr_model_set_casts_shadow), so it floats
 *     like everything did before shadows; the one on the right doesn't receive
 *     (wgr_model_set_receives_shadow), so the wall's shadow passes over it
 *   - UP/DOWN change the shadow distance: less distance covers less of the scene with
 *     the same map, so the shadows get sharper
 *   - [ and ] change the depth bias: too little stripes the lit surfaces ("acne"),
 *     too much lifts a shadow away from what casts it
 *   - M cycles the map size (512, 1024, 2048, 4096)
 *   - S changes how much light a shadow blocks, and T tints what it leaves behind
 *
 * Keys: 1 sun shadows, 2 spot shadows, UP/DOWN distance, [ ] bias, M map size,
 * S strength, T tint, O camera, ESC quit. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "example_assets.h"
#include "wgr.h"

#define WOMAN_CASUAL_PATH "models/woman_casual/woman_casual.glb"

enum { SIZE_COUNT = 4 };
static const int MAP_SIZES[SIZE_COUNT] = {512, 1024, 2048, 4096};

static struct {
    wgr_handle_t scene, camera, sun, spot, spot_marker, woman_casual;
    wgr_handle_t no_cast, no_receive;
    bool shadows, spot_shadows, orbit;
    float distance, bias, strength;
    int size_index, tint_index;
    float angle, time;
} g = {.shadows = true, .spot_shadows = true, .orbit = true, .distance = 30.0f, .bias = 1.0f,
       .strength = 1.0f, .size_index = 1};

/* what a shadow keeps of the light: nothing (physical), then two stylised tints */
enum { TINT_COUNT = 3 };
static const wgr_color_t TINTS[TINT_COUNT] = {0x000000FFu, 0x1E3C64FFu, 0x64321EFFu};
static const char *TINT_NAMES[TINT_COUNT] = {"none", "cool", "warm"};

static void on_model_loaded(const char *path, void *user)
{
    wgr_handle_t mesh = wgr_mesh_create(path);
    (void)user;
    wgr_model_set_mesh(g.woman_casual, mesh);
    wgr_mesh_release(mesh);
    wgr_model_set_animation(g.woman_casual, 3);
    wgr_model_set_animation_loop(g.woman_casual, true);
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    wgr_logger_error("load failed: %s", path);
}

/* A model of `mesh` at (x, y, z) in one color, added to the scene. */
static wgr_handle_t place(wgr_handle_t mesh, float x, float y, float z, float r, float gr, float b, float roughness)
{
    const wgr_handle_t model = wgr_model_create(mesh);
    const wgr_handle_t material = wgr_material_create(WGR_MATERIAL_PBR);
    wgr_mesh_release(mesh); /* the model holds it */
    wgr_model_set_transform(model, x, y, z, 0, 0, 0, 1, 1, 1);
    wgr_material_set_vec4(material, "base_color", r, gr, b, 1.0f);
    wgr_material_set_float(material, "metallic", 0.0f);
    wgr_material_set_float(material, "roughness", roughness);
    wgr_model_set_material(model, -1, material);
    wgr_material_release(material);
    wgr_scene_add(g.scene, model, 0);
    return model;
}

static void init(void *user_data)
{
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);

    g.camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    g.scene = wgr_scene_create();
    wgr_scene_set_active_camera(g.scene, g.camera);
    wgr_scene_set_ambient(g.scene, wgr_color_rgba(140, 170, 225, 255), 0.25f);

    g.sun = wgr_light_create(WGR_LIGHT_DIRECTIONAL);
    wgr_light_set_direction(g.sun, -0.75f, -0.85f, -0.35f);
    wgr_light_set_color(g.sun, wgr_color_rgba(255, 244, 224, 255));
    wgr_light_set_intensity(g.sun, 3.2f);
    wgr_light_set_casts_shadows(g.sun, true);
    wgr_light_set_shadow_distance(g.sun, g.distance);
    wgr_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
    wgr_light_set_shadow_strength(g.sun, g.strength);
    wgr_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    wgr_scene_add(g.scene, g.sun, 0);

    /* a second caster: a spot light that circles the scene and shadows through its
       own cone. Both share the map, so both use the same size */
    g.spot = wgr_light_create(WGR_LIGHT_SPOT);
    wgr_light_set_color(g.spot, wgr_color_rgba(150, 210, 255, 255));
    wgr_light_set_intensity(g.spot, 260.0f);
    wgr_light_set_range(g.spot, 24.0f);
    wgr_light_set_spot_cone(g.spot, 0.30f, 0.44f);
    wgr_light_set_casts_shadows(g.spot, true);
    wgr_light_set_shadow_map_size(g.spot, MAP_SIZES[g.size_index]);
    wgr_light_set_shadow_distance(g.spot, 24.0f);
    wgr_scene_add(g.scene, g.spot, 0);
    g.spot_marker = wgr_shape3d_create();
    wgr_shape3d_set_sphere(g.spot_marker, 0.16f);
    wgr_shape3d_set_color(g.spot_marker, wgr_color_rgba(150, 210, 255, 255));
    wgr_scene_add(g.scene, g.spot_marker, 0);

    place(wgr_mesh_create_plane(40.0f, 40.0f, 0), 0, 0, 0, 0.42f, 0.44f, 0.46f, 0.9f);
    /* a wall to throw a long shadow across the floor */
    place(wgr_mesh_create_cube(0.5f, 3.0f, 7.0f), -4.5f, 1.5f, 0, 0.55f, 0.5f, 0.45f, 0.85f);
    place(wgr_mesh_create_torus(0.7f, 0.22f, 48, 24), 2.6f, 1.1f, -1.6f, 0.9f, 0.55f, 0.2f, 0.4f);
    place(wgr_mesh_create_capsule(0.4f, 1.6f, 16, 32), 1.2f, 0.8f, 1.8f, 0.35f, 0.75f, 0.45f, 0.5f);
    place(wgr_mesh_create_cube(1.0f, 1.0f, 1.0f), 3.8f, 0.5f, 1.4f, 0.3f, 0.5f, 0.85f, 0.6f);

    /* one that casts nothing, and one that nothing shadows */
    g.no_cast = place(wgr_mesh_create_sphere(0.6f, 24, 48), -2.0f, 0.6f, 2.4f, 0.95f, 0.85f, 0.3f, 0.35f);
    wgr_model_set_casts_shadow(g.no_cast, false);
    g.no_receive = place(wgr_mesh_create_sphere(0.6f, 24, 48), -2.6f, 0.6f, -1.2f, 0.9f, 0.3f, 0.5f, 0.35f);
    wgr_model_set_receives_shadow(g.no_receive, false);

    g.woman_casual = wgr_model_create(0);
    wgr_model_set_transform(g.woman_casual, 0, 0, 0, 0, 0, 0, 1, 1, 1);
    wgr_scene_add(g.scene, g.woman_casual, 0);
    wgr_asset_add_task(wgr_asset_ensure_async(WOMAN_CASUAL_PATH, NULL, WGR_ASSET_NONE), on_model_loaded, on_failed, NULL);
    wgr_debug_enable_fps(12, 10, 16);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[128];
    (void)tick_fraction;
    (void)user_data;

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) wgr_request_quit();
    if (wgr_input_get_key(WGR_KEY_1) == WGR_BUTTON_PRESSED) {
        g.shadows = !g.shadows;
        wgr_light_set_casts_shadows(g.sun, g.shadows);
    }
    if (wgr_input_get_key(WGR_KEY_2) == WGR_BUTTON_PRESSED) {
        g.spot_shadows = !g.spot_shadows;
        wgr_light_set_casts_shadows(g.spot, g.spot_shadows);
    }
    if (wgr_input_get_key(WGR_KEY_O) == WGR_BUTTON_PRESSED) g.orbit = !g.orbit;
    if (wgr_input_get_key(WGR_KEY_S) == WGR_BUTTON_PRESSED) {
        g.strength = g.strength > 0.9f ? 0.65f : g.strength > 0.5f ? 0.35f : 1.0f;
        wgr_light_set_shadow_strength(g.sun, g.strength);
    }
    if (wgr_input_get_key(WGR_KEY_T) == WGR_BUTTON_PRESSED) {
        g.tint_index = (g.tint_index + 1) % TINT_COUNT;
        wgr_light_set_shadow_color(g.sun, TINTS[g.tint_index]);
    }
    if (wgr_input_get_key(WGR_KEY_M) == WGR_BUTTON_PRESSED) {
        g.size_index = (g.size_index + 1) % SIZE_COUNT;
        wgr_light_set_shadow_map_size(g.sun, MAP_SIZES[g.size_index]);
        wgr_light_set_shadow_map_size(g.spot, MAP_SIZES[g.size_index]);
    }
    if (wgr_input_get_key(WGR_KEY_UP) != WGR_BUTTON_UP || wgr_input_get_key(WGR_KEY_DOWN) != WGR_BUTTON_UP) {
        const float step = wgr_input_get_key(WGR_KEY_UP) != WGR_BUTTON_UP ? dt * 20.0f : -dt * 20.0f;
        g.distance = fmaxf(2.0f, fminf(g.distance + step, 200.0f));
        wgr_light_set_shadow_distance(g.sun, g.distance);
    }
    if (wgr_input_get_key(WGR_KEY_LEFT_BRACKET) != WGR_BUTTON_UP ||
        wgr_input_get_key(WGR_KEY_RIGHT_BRACKET) != WGR_BUTTON_UP) {
        const float step = wgr_input_get_key(WGR_KEY_RIGHT_BRACKET) != WGR_BUTTON_UP ? dt * 4.0f : -dt * 4.0f;
        g.bias = fmaxf(0.0f, fminf(g.bias + step, 16.0f));
        wgr_light_set_shadow_bias(g.sun, g.bias, g.bias * 4.0f);
    }

    g.time += dt;
    wgr_model_animate(g.woman_casual, dt);
    /* the spot circles overhead, always aimed at the middle of the scene */
    const float sx = 7.0f * sinf(g.time * 0.35f), sz = 7.0f * cosf(g.time * 0.35f);
    wgr_light_set_position(g.spot, sx, 6.5f, sz);
    wgr_light_set_direction(g.spot, -sx, -6.5f, -sz);
    wgr_shape3d_set_transform(g.spot_marker, sx, 6.5f, sz, 0, 0, 0, 1, 1, 1);
    if (g.orbit) g.angle += dt * 0.18f;
    wgr_camera3d_set_view(g.camera, 11.0f * sinf(g.angle), 5.0f, 11.0f * cosf(g.angle), 0, 1.2f, 0, 0, 1, 0);

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(120, 150, 200, 255));
    wgr_scene_draw(g.scene);
    wgr_text_draw("libwgrender shadows: a directional light casting into a depth map", 12, 36, 20, WGR_COLOR_RAYWHITE);
    snprintf(line, sizeof(line), "[1] sun %s   [2] spot %s   map %d   distance %.0f   bias %.1f texels",
             g.shadows ? "on" : "off", g.spot_shadows ? "on" : "off", MAP_SIZES[g.size_index], (double)g.distance,
             (double)g.bias);
    wgr_text_draw(line, 12, 64, 16, WGR_COLOR_LIGHTGRAY);
    snprintf(line, sizeof(line), "[S] strength %.2f   [T] tint %s", (double)g.strength, TINT_NAMES[g.tint_index]);
    wgr_text_draw(line, 12, 86, 16, WGR_COLOR_LIGHTGRAY);
    wgr_text_draw("UP/DOWN distance   [ ] bias   M map size   O camera   ESC quit", 12, 108, 16, WGR_COLOR_GRAY);
    wgr_text_draw("left ball casts nothing; right ball receives nothing", 12, 130, 16, WGR_COLOR_GRAY);
    wgr_render_end_frame();
}

int main(void)
{
    wgr_init_values(1000, 600, "libwgrender shadows", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
