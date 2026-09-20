#ifndef WGR_TESTS_H
#define WGR_TESTS_H

/* Every unit test; add new ones here and to the table in main.c. */

void test_asset_join_relative(void);
void test_asset_fetch_hook(void);

void test_audio_streaming(void);
void test_audio_threads(void);
void test_audio_many_sounds(void);

void test_environment_mapping(void);
void test_environment_irradiance(void);
void test_environment_prefilter(void);
void test_environment_brdf_lut(void);
void test_environment_brdf_lut_baked(void);
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
void test_input_wheel(void);
void test_input_capture(void);
void test_input_touches(void);
void test_input_touch_gesture(void);

void test_sprite2d_corners(void);
void test_sprite2d_screen_to_unit(void);

void test_tick_clock_rate(void);
void test_tick_clock_stall(void);

void test_handle_pool(void);
void test_handle_pool_reuse(void);
void test_handle_pool_fifo(void);
void test_handle_pool_growable(void);

void test_light_falloff(void);
void test_light_select(void);
void test_light_api(void);

void test_material_srgb(void);
void test_material_api(void);
void test_material_uv_matrix(void);
void test_shader_custom_material(void);
void test_shader_sprites(void);
void test_shader_effects(void);
void test_shadow_state(void);
void test_shadow_fit(void);
void test_shadow_fit_spot(void);
void test_shadow_caster_cull(void);
void test_shadow_instancing(void);
void test_shadow_casters(void);
void test_model_draw_queue(void);
void test_cull_frustum(void);
void test_cull_scene(void);
void test_model_instance_record(void);
void test_model_instancing(void);
void test_animation_sampling(void);
void test_sprite_pick_alpha(void);
void test_text2d_state(void);
void test_scene_layer_order(void);
void test_render_command_merging(void);
void test_render_command_passes(void);
void test_fs_paths(void);
void test_fs_files(void);

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
void test_runtime_capabilities(void);
void test_interaction(void);
void test_shape2d(void);
void test_shape2d_immediate(void);
void test_text_default_font(void);
void test_text_slices_and_dpi(void);
void test_text_font_refcount(void);

void test_pipeline_gpu_pools(void);
void test_pipeline_mesh_textures(void);
void test_pipeline_async(void);
void test_pipeline_unclaimed(void);
void test_pipeline_failures(void);
void test_pipeline_budget(void);
void test_pipeline_shutdown(void);
void test_pipeline_group(void);
void test_pipeline_many(void);

void test_pick_object(void);
void test_shape_3d(void);
void test_pick_ray_sphere(void);
void test_pick_ray_aabb(void);
void test_pick_ray_triangle(void);
void test_pick_ray_to_local(void);
void test_pick_world_aabb(void);
void test_pick_ray_from_screen(void);

void test_render_targets(void);
void test_render_clip_stack(void);
void test_render_sgl_growth(void);
void test_texture_draw_immediate(void);

void test_nine_slice(void);
void test_text2d_layout(void);
void test_text_layout_shared(void);
void test_color_values(void);
void test_scene_clip(void);
void test_sprite3d_2d_world(void);
void test_sprite3d_facings(void);
void test_sprite_pools_grow(void);
void test_sprites_interleaved(void);
void test_sprite3d_alpha_modes(void);
void test_sprite2d_batches(void);
void test_emitter_spawning(void);
void test_emitter_particles(void);
void test_emitter_scene(void);
void test_emitter_motion(void);
void test_emitter_curves(void);
void test_emitter_start(void);
void test_module_registry(void);
void test_gamepad_buttons(void);
void test_gamepad_axes(void);
void test_ktx_parse(void);
void test_ktx_variants(void);
void test_ktx_load(void);
void test_pipeline_gltf_ktx(void);
void test_pipeline_ktx_fallback(void);
void test_pipeline_redirects(void);
void test_pipeline_generated_meshes(void);
void test_pipeline_skinned_joints(void);
void test_mesh_shapes(void);

void test_scene_view_depth(void);
void test_scene_sort_transparent(void);
void test_scene_membership(void);
void test_scene_sort_transparent_many(void);

#endif // WGR_TESTS_H
