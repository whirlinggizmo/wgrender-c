#ifndef WGR_INTERNAL_EMITTER_H
#define WGR_INTERNAL_EMITTER_H

#include <stdbool.h>

#include "wgr_types.h"

/* Particle emitters (wgr_emitter.c; public API in wgr_emitter3d.h and wgr_emitter2d.h). */
void wgr_emitter_init(void);
void wgr_emitter_deinit(void);

/* The runtime: advance every emitter by the frame's time (spawn, retire the dead),
 * once a frame before the frame callback. */
void wgr_emitter_update(float dt);

/* wgr_render: upload the frame's particles (before any pass), and start over (after
 * the frame is submitted). */
void wgr_emitter_flush(void);
void wgr_emitter_end_frame(void);

/* For tests: particle `index` (0 = the oldest in the emitter's window) as written at
 * its birth: born xyz + time, velocity xyz + life, shape (size scale, spin, angle, a
 * random 0..1). False past the window. */
bool wgr_emitter_particle(wgr_handle_t emitter, int index, float born[4], float motion[4], float shape[4]);
/* For tests: the size curve's keys (in order) and how many. */
int wgr_emitter_size_keys(wgr_handle_t emitter, float times[8], float values[8]);

#endif // WGR_INTERNAL_EMITTER_H
