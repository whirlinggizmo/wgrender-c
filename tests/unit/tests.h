#ifndef SK_TESTS_H
#define SK_TESTS_H

/* Every unit test; add new ones here and to the table in main.c. */

void test_frame_pace_unpaced(void);
void test_frame_pace_schedule(void);
void test_frame_pace_web_skip(void);

void test_input_tick_edges(void);
void test_input_tick_deltas(void);

void test_tick_clock_rate(void);
void test_tick_clock_stall(void);

void test_handle_pool(void);
void test_handle_pool_reuse(void);

void test_math_inverse(void);
void test_math_trs(void);

void test_pick_ray_sphere(void);
void test_pick_ray_aabb(void);
void test_pick_ray_triangle(void);
void test_pick_ray_to_local(void);
void test_pick_world_aabb(void);
void test_pick_ray_from_screen(void);

void test_scene_view_depth(void);
void test_scene_sort_transparent(void);

#endif // SK_TESTS_H
