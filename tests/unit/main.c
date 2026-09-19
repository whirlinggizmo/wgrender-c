/* libsk unit test runner (`make test`).
 *
 *   tests/build/unit_tests            run every test
 *   tests/build/unit_tests pick_      run tests whose name starts with "pick_"
 *
 * Tests call library internals directly and link against build/headless/libsk.a. Nothing
 * here opens a window or touches the GPU. */
#include <stdio.h>
#include <string.h>

#include "test.h"
#include "tests.h"

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case_t;

static const test_case_t TESTS[] = {
    {"asset_join_relative", test_asset_join_relative},
    {"audio_streaming", test_audio_streaming},
    {"audio_threads", test_audio_threads},
    {"audio_many_sounds", test_audio_many_sounds},
    {"environment_mapping", test_environment_mapping},
    {"environment_irradiance", test_environment_irradiance},
    {"environment_prefilter", test_environment_prefilter},
    {"environment_brdf_lut", test_environment_brdf_lut},
    {"environment_brdf_lut_baked", test_environment_brdf_lut_baked},
    {"environment_half_float", test_environment_half_float},
    {"environment_api", test_environment_api},
    {"camera_api", test_camera_api},
    {"camera_projection", test_camera_projection},
    {"pick_ray_from_screen_ortho", test_pick_ray_from_screen_ortho},
    {"frame_pace_unpaced", test_frame_pace_unpaced},
    {"frame_pace_schedule", test_frame_pace_schedule},
    {"frame_pace_web_skip", test_frame_pace_web_skip},
    {"input_tick_edges", test_input_tick_edges},
    {"input_tick_deltas", test_input_tick_deltas},
    {"input_wheel", test_input_wheel},
    {"input_capture", test_input_capture},
    {"input_touches", test_input_touches},
    {"input_touch_gesture", test_input_touch_gesture},
    {"sprite2d_corners", test_sprite2d_corners},
    {"sprite2d_screen_to_unit", test_sprite2d_screen_to_unit},
    {"tick_clock_rate", test_tick_clock_rate},
    {"tick_clock_stall", test_tick_clock_stall},
    {"handle_pool", test_handle_pool},
    {"handle_pool_reuse", test_handle_pool_reuse},
    {"handle_pool_fifo", test_handle_pool_fifo},
    {"handle_pool_growable", test_handle_pool_growable},
    {"light_falloff", test_light_falloff},
    {"light_select", test_light_select},
    {"light_api", test_light_api},
    {"material_srgb", test_material_srgb},
    {"material_api", test_material_api},
    {"material_uv_matrix", test_material_uv_matrix},
    {"math_angles", test_math_angles},
    {"math_inverse", test_math_inverse},
    {"math_trs", test_math_trs},
    {"model_skin_position", test_model_skin_position},
    {"model_sample_alpha", test_model_sample_alpha},
    {"model_generate_tangents", test_model_generate_tangents},
    {"parity_text3d", test_parity_text3d},
    {"parity_sprite3d", test_parity_sprite3d},
    {"parity_model", test_parity_model},
    {"parity_sound_pan", test_parity_sound_pan},
    {"parity_asset_host", test_parity_asset_host},
    {"window_headless", test_window_headless},
    {"interaction", test_interaction},
    {"shape2d", test_shape2d},
    {"shape2d_immediate", test_shape2d_immediate},
    {"nine_slice", test_nine_slice},
    {"text2d_layout", test_text2d_layout},
    {"text_layout_shared", test_text_layout_shared},
    {"color_values", test_color_values},
    {"scene_clip", test_scene_clip},
    {"sprite3d_2d_world", test_sprite3d_2d_world},
    {"sprite3d_facings", test_sprite3d_facings},
    {"sprite_pools_grow", test_sprite_pools_grow},
    {"sprites_interleaved", test_sprites_interleaved},
    {"sprite3d_alpha_modes", test_sprite3d_alpha_modes},
    {"sprite2d_batches", test_sprite2d_batches},
    {"emitter_spawning", test_emitter_spawning},
    {"emitter_particles", test_emitter_particles},
    {"emitter_scene", test_emitter_scene},
    {"emitter_motion", test_emitter_motion},
    {"emitter_curves", test_emitter_curves},
    {"emitter_start", test_emitter_start},
    {"module_registry", test_module_registry},
    {"gamepad_buttons", test_gamepad_buttons},
    {"gamepad_axes", test_gamepad_axes},
    {"ktx_parse", test_ktx_parse},
    {"ktx_variants", test_ktx_variants},
    {"ktx_load", test_ktx_load},
    {"pipeline_gltf_ktx", test_pipeline_gltf_ktx},
    {"text_default_font", test_text_default_font},
    {"text_slices_and_dpi", test_text_slices_and_dpi},
    {"text_font_refcount", test_text_font_refcount},
    {"pipeline_gpu_pools", test_pipeline_gpu_pools},
    {"pipeline_mesh_textures", test_pipeline_mesh_textures},
    {"pipeline_async", test_pipeline_async},
    {"pipeline_unclaimed", test_pipeline_unclaimed},
    {"pipeline_failures", test_pipeline_failures},
    {"pipeline_budget", test_pipeline_budget},
    {"pipeline_shutdown", test_pipeline_shutdown},
    {"pipeline_group", test_pipeline_group},
    {"pipeline_many", test_pipeline_many},
    {"pick_object", test_pick_object},
    {"shape_3d", test_shape_3d},
    {"pick_ray_sphere", test_pick_ray_sphere},
    {"pick_ray_aabb", test_pick_ray_aabb},
    {"pick_ray_triangle", test_pick_ray_triangle},
    {"pick_ray_to_local", test_pick_ray_to_local},
    {"pick_world_aabb", test_pick_world_aabb},
    {"pick_ray_from_screen", test_pick_ray_from_screen},
    {"render_targets", test_render_targets},
    {"render_clip_stack", test_render_clip_stack},
    {"render_sgl_growth", test_render_sgl_growth},
    {"texture_draw_immediate", test_texture_draw_immediate},
    {"scene_view_depth", test_scene_view_depth},
    {"scene_sort_transparent", test_scene_sort_transparent},
    {"scene_membership", test_scene_membership},
    {"scene_sort_transparent_many", test_scene_sort_transparent_many},
};

int sk_test_failures;

static int matches_filter(const char *name, int argc, char **argv)
{
    if (argc <= 1) {
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        if (strncmp(name, argv[i], strlen(argv[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    int run = 0, failed = 0;

    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); i++) {
        if (!matches_filter(TESTS[i].name, argc, argv)) {
            continue;
        }
        sk_test_failures = 0;
        TESTS[i].fn();
        run++;
        if (sk_test_failures > 0) {
            failed++;
            printf("  FAIL  %s (%d failed check%s)\n", TESTS[i].name, sk_test_failures,
                   sk_test_failures == 1 ? "" : "s");
        } else {
            printf("  ok    %s\n", TESTS[i].name);
        }
    }

    if (run == 0) {
        printf("unit tests: no test matched\n");
        return 1;
    }
    printf("%s: %d of %d unit tests passed\n", failed ? "FAIL" : "PASS", run - failed, run);
    return failed ? 1 : 0;
}
