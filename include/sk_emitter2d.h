#ifndef SK_EMITTER2D_H
#define SK_EMITTER2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Particle emitters in screen space: the same as sk_emitter3d.h in 2D. Positions,
 * sizes and velocities are logical pixels (top-left origin, y down), gravity pixels
 * per second squared; `spread` turns the velocity by up to that many radians either
 * way. In a scene a 2D emitter draws with the 2D members, in layer and member order. */

sk_handle_t sk_emitter2d_create(sk_handle_t texture);
void sk_emitter2d_destroy(sk_handle_t emitter);

bool sk_emitter2d_set_source(sk_handle_t emitter, float x, float y, float width, float height);
bool sk_emitter2d_set_position(sk_handle_t emitter, float x, float y);
bool sk_emitter2d_jump(sk_handle_t emitter, float x, float y);
vec2_t sk_emitter2d_get_position(sk_handle_t emitter);

bool sk_emitter2d_set_rate(sk_handle_t emitter, float per_second);
bool sk_emitter2d_burst(sk_handle_t emitter, int count);
bool sk_emitter2d_set_emitting(sk_handle_t emitter, bool emitting);
bool sk_emitter2d_is_emitting(sk_handle_t emitter);
bool sk_emitter2d_set_max(sk_handle_t emitter, int count);
bool sk_emitter2d_set_life(sk_handle_t emitter, float min_seconds, float max_seconds);

bool sk_emitter2d_set_spawn_box(sk_handle_t emitter, float half_width, float half_height);
bool sk_emitter2d_set_velocity(sk_handle_t emitter, float x, float y, float spread, float speed_variance);
bool sk_emitter2d_set_gravity(sk_handle_t emitter, float x, float y);
bool sk_emitter2d_set_drag(sk_handle_t emitter, float per_second);
bool sk_emitter2d_set_inherit_velocity(sk_handle_t emitter, float fraction);

bool sk_emitter2d_set_size(sk_handle_t emitter, float start, float end, float variance);
bool sk_emitter2d_set_color(sk_handle_t emitter, sk_color_t start, sk_color_t end);
bool sk_emitter2d_add_size_key(sk_handle_t emitter, float t, float size);
bool sk_emitter2d_clear_size_keys(sk_handle_t emitter);
bool sk_emitter2d_add_color_key(sk_handle_t emitter, float t, sk_color_t color);
bool sk_emitter2d_clear_color_keys(sk_handle_t emitter);
bool sk_emitter2d_add_palette_color(sk_handle_t emitter, sk_color_t color);
bool sk_emitter2d_clear_palette(sk_handle_t emitter);
bool sk_emitter2d_set_spin(sk_handle_t emitter, float min, float max);
bool sk_emitter2d_set_stretch(sk_handle_t emitter, float seconds);

bool sk_emitter2d_set_alpha_mode(sk_handle_t emitter, sk_alpha_mode_t mode, float cutoff);
bool sk_emitter2d_set_seed(sk_handle_t emitter, unsigned int seed);

int sk_emitter2d_get_count(sk_handle_t emitter);
void sk_emitter2d_clear(sk_handle_t emitter);
bool sk_emitter2d_set_visible(sk_handle_t emitter, bool visible);
void sk_emitter2d_draw(sk_handle_t emitter); /* immediate, in 2D; or add it to a scene */

#ifdef __cplusplus
}
#endif

#endif // SK_EMITTER2D_H
