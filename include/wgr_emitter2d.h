#ifndef WGR_EMITTER2D_H
#define WGR_EMITTER2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Particle emitters in screen space: the same as wgr_emitter3d.h in 2D. Positions,
 * sizes and velocities are logical pixels (top-left origin, y down), gravity pixels
 * per second squared; `spread` turns the velocity by up to that many radians either
 * way. In a scene a 2D emitter draws with the 2D members, in layer and member order. */

wgr_handle_t wgr_emitter2d_create(wgr_handle_t texture);
void wgr_emitter2d_destroy(wgr_handle_t emitter);

bool wgr_emitter2d_set_source(wgr_handle_t emitter, float x, float y, float width, float height);
bool wgr_emitter2d_set_frames(wgr_handle_t emitter, int columns, int rows, int count, float per_second);
bool wgr_emitter2d_set_position(wgr_handle_t emitter, float x, float y);
bool wgr_emitter2d_jump(wgr_handle_t emitter, float x, float y);
vec2_t wgr_emitter2d_get_position(wgr_handle_t emitter);

bool wgr_emitter2d_set_rate(wgr_handle_t emitter, float per_second);
bool wgr_emitter2d_burst(wgr_handle_t emitter, int count);
bool wgr_emitter2d_set_emitting(wgr_handle_t emitter, bool emitting);
bool wgr_emitter2d_is_emitting(wgr_handle_t emitter);
bool wgr_emitter2d_set_max(wgr_handle_t emitter, int count);
bool wgr_emitter2d_set_life(wgr_handle_t emitter, float min_seconds, float max_seconds);
bool wgr_emitter2d_prewarm(wgr_handle_t emitter, float seconds);

bool wgr_emitter2d_set_spawn_box(wgr_handle_t emitter, float half_width, float half_height);
bool wgr_emitter2d_set_spawn_circle(wgr_handle_t emitter, float radius); /* 3D's sphere */
bool wgr_emitter2d_set_velocity(wgr_handle_t emitter, float x, float y, float spread, float speed_variance);
bool wgr_emitter2d_set_gravity(wgr_handle_t emitter, float x, float y);
bool wgr_emitter2d_set_drag(wgr_handle_t emitter, float per_second);
bool wgr_emitter2d_set_inherit_velocity(wgr_handle_t emitter, float fraction);

bool wgr_emitter2d_set_size(wgr_handle_t emitter, float start, float end, float variance);
bool wgr_emitter2d_set_color(wgr_handle_t emitter, wgr_color_t start, wgr_color_t end);
bool wgr_emitter2d_add_size_key(wgr_handle_t emitter, float t, float size);
bool wgr_emitter2d_clear_size_keys(wgr_handle_t emitter);
bool wgr_emitter2d_add_color_key(wgr_handle_t emitter, float t, wgr_color_t color);
bool wgr_emitter2d_clear_color_keys(wgr_handle_t emitter);
bool wgr_emitter2d_add_palette_color(wgr_handle_t emitter, wgr_color_t color);
bool wgr_emitter2d_clear_palette(wgr_handle_t emitter);
bool wgr_emitter2d_set_spin(wgr_handle_t emitter, float min, float max);
bool wgr_emitter2d_set_stretch(wgr_handle_t emitter, float seconds);

bool wgr_emitter2d_set_alpha_mode(wgr_handle_t emitter, wgr_alpha_mode_t mode, float cutoff);
bool wgr_emitter2d_set_seed(wgr_handle_t emitter, unsigned int seed);

int wgr_emitter2d_get_count(wgr_handle_t emitter);
void wgr_emitter2d_clear(wgr_handle_t emitter);
bool wgr_emitter2d_set_visible(wgr_handle_t emitter, bool visible);
void wgr_emitter2d_draw(wgr_handle_t emitter); /* immediate, in 2D; or add it to a scene */

#ifdef __cplusplus
}
#endif

#endif // WGR_EMITTER2D_H
