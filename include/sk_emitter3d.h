#ifndef SK_EMITTER3D_H
#define SK_EMITTER3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Particle emitters in the 3D world (docs/PLAN-sprites.md, step 4). An emitter is one
 * object that owns many particles, drawn from a texture (Texture -> Emitter, as
 * Texture -> Sprite). A particle is decided when it's born (where, how fast, how long
 * it lives, how big, how it spins, all within the ranges set here) and the GPU works
 * out where it is from its age: gravity pulls it, and its size and color move from
 * their start values to their end values over its life. The CPU only spawns, so
 * thousands of particles cost about what spawning them does.
 *
 * libsk advances every emitter once a frame, by the frame's time. Particles stay where
 * they were born when the emitter moves (trails), face the camera, and aren't sorted
 * among themselves; in a scene an emitter is one member (sk_scene_add), sorted as a
 * whole when blended. Destroying an emitter takes it out of its scenes. */

sk_handle_t sk_emitter3d_create(sk_handle_t texture);
void sk_emitter3d_destroy(sk_handle_t emitter);

/* Region of the texture each particle shows, in texture pixels (an atlas cell).
 * Default: the whole texture; width or height <= 0 resets to that. */
bool sk_emitter3d_set_source(sk_handle_t emitter, float x, float y, float width, float height);

/* set_position moves the emitter: the next frame's steady spawns are spread along the
 * way from where it was (a smooth trail however fast it goes), and the move is the
 * velocity particles inherit (set_inherit_velocity). The first position isn't a move.
 * jump puts it somewhere without either: nothing spawns along the way. */
bool sk_emitter3d_set_position(sk_handle_t emitter, float x, float y, float z);
bool sk_emitter3d_jump(sk_handle_t emitter, float x, float y, float z);
vec3_t sk_emitter3d_get_position(sk_handle_t emitter);

/* Emission: a steady rate (particles per second; 0 for bursts only), and bursts of
 * `count` at once. set_emitting(false) stops the steady rate; the particles alive
 * finish their lives. At most `max` are alive at once (default 1024): new ones
 * replace the oldest. Life: each particle's, seconds, between min and max. */
bool sk_emitter3d_set_rate(sk_handle_t emitter, float per_second);
bool sk_emitter3d_burst(sk_handle_t emitter, int count);
bool sk_emitter3d_set_emitting(sk_handle_t emitter, bool emitting);
bool sk_emitter3d_is_emitting(sk_handle_t emitter);
bool sk_emitter3d_set_max(sk_handle_t emitter, int count);
bool sk_emitter3d_set_life(sk_handle_t emitter, float min_seconds, float max_seconds);

/* Birth: anywhere in a box around the position (half sizes; default a point), moving
 * along (x, y, z) at its length's speed, turned up to `spread` radians off it (a
 * cone) and faster or slower by up to `speed_variance` (0..1) of it. Gravity: an
 * acceleration, world units per second squared (default none). */
bool sk_emitter3d_set_spawn_box(sk_handle_t emitter, float half_x, float half_y, float half_z);
bool sk_emitter3d_set_velocity(sk_handle_t emitter, float x, float y, float z, float spread, float speed_variance);
bool sk_emitter3d_set_gravity(sk_handle_t emitter, float x, float y, float z);

/* After birth: drag slows particles in proportion to their speed (per second: 1 loses
 * about 63% of the speed in a second, less gravity's pull; gravity / drag is how fast
 * they end up falling). Inherit velocity adds that fraction of the emitter's own
 * velocity (its movement, from set_position) at birth: 1 carries them along with it.
 * Defaults 0. */
bool sk_emitter3d_set_drag(sk_handle_t emitter, float per_second);
bool sk_emitter3d_set_inherit_velocity(sk_handle_t emitter, float fraction);

/* Over a particle's life: its size (world units; each particle's scaled by up to
 * `variance`, 0..1) and color (tint, alpha included, so a fade) move from start to
 * end. Spin: radians per second between min and max, positive clockwise on screen,
 * from a random angle. Defaults: size 1 -> 1, white -> white, no spin. */
bool sk_emitter3d_set_size(sk_handle_t emitter, float start, float end, float variance);
bool sk_emitter3d_set_color(sk_handle_t emitter, sk_color_t start, sk_color_t end);

/* Curves: set_size and set_color make two keys, at 0 (birth) and 1 (death). For more,
 * clear the keys and add up to 8, each at `t` (0..1 of a particle's life); between two
 * keys the value moves in a line, before the first and after the last it holds. Keys
 * at the same time make a step. An emitter without size or color keys draws nothing. */
bool sk_emitter3d_add_size_key(sk_handle_t emitter, float t, float size);
bool sk_emitter3d_clear_size_keys(sk_handle_t emitter);
bool sk_emitter3d_add_color_key(sk_handle_t emitter, float t, sk_color_t color);
bool sk_emitter3d_clear_color_keys(sk_handle_t emitter);

/* Palette: up to 8 colors; each particle picks one at birth, and it tints the color over
 * its life (confetti from one emitter). Empty by default: no tint. */
bool sk_emitter3d_add_palette_color(sk_handle_t emitter, sk_color_t color);
bool sk_emitter3d_clear_palette(sk_handle_t emitter);
bool sk_emitter3d_set_spin(sk_handle_t emitter, float min, float max);
/* Stretch along the motion (sparks, rain, streaks): each particle's texture points its
 * top along its velocity across the screen, and is as long as the distance it moves in
 * `seconds` (plus its size), trailing behind it. Stretched particles don't spin.
 * Default 0: off. */
bool sk_emitter3d_set_stretch(sk_handle_t emitter, float seconds);

/* How particles use alpha (default SK_ALPHA_ADD: glows, sparks, fire). */
bool sk_emitter3d_set_alpha_mode(sk_handle_t emitter, sk_alpha_mode_t mode, float cutoff);
/* Where the emitter's random numbers start: the same seed and settings give the same
 * particles (default: a different seed per emitter). */
bool sk_emitter3d_set_seed(sk_handle_t emitter, unsigned int seed);

int sk_emitter3d_get_count(sk_handle_t emitter); /* particles alive now */
void sk_emitter3d_clear(sk_handle_t emitter);    /* all of them gone */
bool sk_emitter3d_set_visible(sk_handle_t emitter, bool visible);
void sk_emitter3d_draw(sk_handle_t emitter);     /* immediate, in 3D mode; or add it to a scene */

#ifdef __cplusplus
}
#endif

#endif // SK_EMITTER3D_H
