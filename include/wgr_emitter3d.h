#ifndef WGR_EMITTER3D_H
#define WGR_EMITTER3D_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Particle emitters in the 3D world (docs/PLAN-sprites.md, step 4). An emitter is one
 * object that owns many particles, drawn from a texture (Texture -> Emitter, as
 * Texture -> Sprite). A particle is decided when it's born (where, how fast, how long
 * it lives, how big, how it spins, all within the ranges set here) and the GPU works
 * out where it is from its age: gravity pulls it, and its size and color move from
 * their start values to their end values over its life. The CPU only spawns, so
 * thousands of particles cost about what spawning them does.
 *
 * libwgrender advances every emitter once a frame, by the frame's time. Particles stay where
 * they were born when the emitter moves (trails), face the camera, and aren't sorted
 * among themselves; in a scene an emitter is one member (wgr_scene_add), sorted as a
 * whole when blended. Destroying an emitter takes it out of its scenes. */

wgr_handle_t wgr_emitter3d_create(wgr_handle_t texture);
void wgr_emitter3d_destroy(wgr_handle_t emitter);

/* Region of the texture each particle shows, in texture pixels (an atlas cell).
 * Default: the whole texture; width or height <= 0 resets to that. */
bool wgr_emitter3d_set_source(wgr_handle_t emitter, float x, float y, float width, float height);
/* Flipbook: the source split into `columns` x `rows` frames (left to right, top to
 * bottom), of which the first `count` are used (<= 0: all). With `per_second` 0 each
 * particle plays them once over its life (puffs, explosions); above 0 it loops at that
 * rate from a random frame (flames). Default: one frame. */
bool wgr_emitter3d_set_frames(wgr_handle_t emitter, int columns, int rows, int count, float per_second);

/* set_position moves the emitter: the next frame's steady spawns are spread along the
 * way from where it was (a smooth trail however fast it goes), and the move is the
 * velocity particles inherit (set_inherit_velocity). The first position isn't a move.
 * jump puts it somewhere without either: nothing spawns along the way. */
bool wgr_emitter3d_set_position(wgr_handle_t emitter, float x, float y, float z);
bool wgr_emitter3d_jump(wgr_handle_t emitter, float x, float y, float z);
vec3_t wgr_emitter3d_get_position(wgr_handle_t emitter);

/* Emission: a steady rate (particles per second; 0 for bursts only), and bursts of
 * `count` at once. set_emitting(false) stops the steady rate; the particles alive
 * finish their lives. At most `max` are alive at once (default 1024, capped at
 * 65536): new ones replace the oldest. Life: each particle's, seconds, between min
 * and max.
 *
 * Particles are unlit: the texture times the particle's color, with no material and
 * no scene lighting (a lit effect wants sprite3d objects with a material). */
bool wgr_emitter3d_set_rate(wgr_handle_t emitter, float per_second);
bool wgr_emitter3d_burst(wgr_handle_t emitter, int count);
bool wgr_emitter3d_set_emitting(wgr_handle_t emitter, bool emitting);
bool wgr_emitter3d_is_emitting(wgr_handle_t emitter);
bool wgr_emitter3d_set_max(wgr_handle_t emitter, int count);
bool wgr_emitter3d_set_life(wgr_handle_t emitter, float min_seconds, float max_seconds);
/* Start over as if the steady rate had been running for `seconds` (smoke already rising
 * when a scene appears): the particles alive go, and those the rate would have made
 * over that time, and still alive now, are made where the emitter is. */
bool wgr_emitter3d_prewarm(wgr_handle_t emitter, float seconds);

/* Birth: anywhere in a box around the position (half sizes; default a point), or evenly
 * within a sphere (each replaces the other), moving
 * along (x, y, z) at its length's speed, turned up to `spread` radians off it (a
 * cone) and faster or slower by up to `speed_variance` (0..1) of it. Gravity: an
 * acceleration, world units per second squared (default none). */
bool wgr_emitter3d_set_spawn_box(wgr_handle_t emitter, float half_x, float half_y, float half_z);
bool wgr_emitter3d_set_spawn_sphere(wgr_handle_t emitter, float radius);
bool wgr_emitter3d_set_velocity(wgr_handle_t emitter, float x, float y, float z, float spread, float speed_variance);
bool wgr_emitter3d_set_gravity(wgr_handle_t emitter, float x, float y, float z);

/* After birth: drag slows particles in proportion to their speed (per second: 1 loses
 * about 63% of the speed in a second, less gravity's pull; gravity / drag is how fast
 * they end up falling). Inherit velocity adds that fraction of the emitter's own
 * velocity (its movement, from set_position) at birth: 1 carries them along with it.
 * Defaults 0. */
bool wgr_emitter3d_set_drag(wgr_handle_t emitter, float per_second);
bool wgr_emitter3d_set_inherit_velocity(wgr_handle_t emitter, float fraction);

/* Over a particle's life: its size (world units; each particle's scaled by up to
 * `variance`, 0..1) and color (tint, alpha included, so a fade) move from start to
 * end. Spin: radians per second between min and max, positive clockwise on screen,
 * from a random angle. Defaults: size 1 -> 1, white -> white, no spin. */
bool wgr_emitter3d_set_size(wgr_handle_t emitter, float start, float end, float variance);
bool wgr_emitter3d_set_color(wgr_handle_t emitter, wgr_color_t start, wgr_color_t end);

/* Curves: set_size and set_color make two keys, at 0 (birth) and 1 (death). For more,
 * clear the keys and add up to 8, each at `t` (0..1 of a particle's life); between two
 * keys the value moves in a line, before the first and after the last it holds. Keys
 * at the same time make a step. An emitter without size or color keys draws nothing. */
bool wgr_emitter3d_add_size_key(wgr_handle_t emitter, float t, float size);
bool wgr_emitter3d_clear_size_keys(wgr_handle_t emitter);
bool wgr_emitter3d_add_color_key(wgr_handle_t emitter, float t, wgr_color_t color);
bool wgr_emitter3d_clear_color_keys(wgr_handle_t emitter);

/* Palette: up to 8 colors; each particle picks one at birth, and it tints the color over
 * its life (confetti from one emitter). Empty by default: no tint. */
bool wgr_emitter3d_add_palette_color(wgr_handle_t emitter, wgr_color_t color);
bool wgr_emitter3d_clear_palette(wgr_handle_t emitter);
bool wgr_emitter3d_set_spin(wgr_handle_t emitter, float min, float max);
/* Stretch along the motion (sparks, rain, streaks): each particle's texture points its
 * top along its velocity across the screen, and is as long as the distance it moves in
 * `seconds` (plus its size), trailing behind it. Stretched particles don't spin.
 * Default 0: off. */
bool wgr_emitter3d_set_stretch(wgr_handle_t emitter, float seconds);

/* How particles use alpha (default WGR_ALPHA_ADD: glows, sparks, fire). */
bool wgr_emitter3d_set_alpha_mode(wgr_handle_t emitter, wgr_alpha_mode_t mode, float cutoff);
/* Where the emitter's random numbers start: the same seed and settings give the same
 * particles (default: a different seed per emitter). */
bool wgr_emitter3d_set_seed(wgr_handle_t emitter, unsigned int seed);

int wgr_emitter3d_get_count(wgr_handle_t emitter); /* particles alive now */
void wgr_emitter3d_clear(wgr_handle_t emitter);    /* all of them gone */
bool wgr_emitter3d_set_visible(wgr_handle_t emitter, bool visible);
void wgr_emitter3d_draw(wgr_handle_t emitter);     /* immediate, in 3D mode; or add it to a scene */

#ifdef __cplusplus
}
#endif

#endif // WGR_EMITTER3D_H
