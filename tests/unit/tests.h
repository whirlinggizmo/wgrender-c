#ifndef SK_TESTS_H
#define SK_TESTS_H

/* Every unit test; add new ones here and to the table in main.c. */

void test_asset_join_relative(void);

void test_audio_streaming(void);
void test_audio_threads(void);

void test_environment_mapping(void);
void test_environment_irradiance(void);
void test_environment_prefilter(void);
void test_environment_brdf_lut(void);
void test_environment_half_float(void);
void test_environment_api(void);

void test_camera_api(void);
void test_camera_projection(void);
void test_pick_ray_from_screen_ortho(void);

void test_frame_pace_unpaced(void);
void test_frame_pace_schedule(void);
void test_frame_pace_web_skip(void);

void test_input_tick_edges(void);
void test_input_tick_deltas(void);

void test_sprite2d_corners(void);
void test_sprite2d_screen_to_unit(void);

void test_tick_clock_rate(void);
void test_tick_clock_stall(void);

void test_handle_pool(void);
void test_handle_pool_reuse(void);

void test_light_falloff(void);
void test_light_select(void);
void test_light_api(void);

void test_material_srgb(void);
void test_material_api(void);
void test_material_uv_matrix(void);

void test_math_angles(void);
void test_math_inverse(void);
void test_math_trs(void);

void test_model_skin_position(void);
void test_model_sample_alpha(void);
void test_model_generate_tangents(void);

void test_parity_text3d(void);
void test_parity_sprite3d(void);
void test_parity_model(void);
void test_parity_sound_pan(void);
void test_parity_asset_host(void);

void test_window_headless(void);
void test_interaction(void);
void test_shape2d(void);
void test_text_default_font(void);
void test_text_font_refcount(void);

void test_pipeline_gpu_pools(void);
void test_pipeline_mesh_textures(void);
void test_pipeline_async(void);
void test_pipeline_unclaimed(void);
void test_pipeline_failures(void);
void test_pipeline_budget(void);
void test_pipeline_shutdown(void);
void test_pipeline_group(void);

void test_pick_object(void);
void test_shape_3d(void);
void test_pick_ray_sphere(void);
void test_pick_ray_aabb(void);
void test_pick_ray_triangle(void);
void test_pick_ray_to_local(void);
void test_pick_world_aabb(void);
void test_pick_ray_from_screen(void);

void test_render_targets(void);

void test_nine_slice(void);
void test_text2d_layout(void);
void test_scene_clip(void);
void test_sprite3d_2d_world(void);

void test_scene_view_depth(void);
void test_scene_sort_transparent(void);

#endif // SK_TESTS_H
