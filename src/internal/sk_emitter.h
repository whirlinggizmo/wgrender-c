#ifndef SK_INTERNAL_EMITTER_H
#define SK_INTERNAL_EMITTER_H

#include <stdbool.h>

#include "sk_types.h"

/* Particle emitters (sk_emitter.c; public API in sk_emitter3d.h and sk_emitter2d.h). */
void sk_emitter_init(void);
void sk_emitter_deinit(void);

/* The runtime: advance every emitter by the frame's time (spawn, retire the dead),
 * once a frame before the frame callback. */
void sk_emitter_update(float dt);

/* sk_render: upload the frame's particles (before any pass), and start over (after
 * the frame is submitted). */
void sk_emitter_flush(void);
void sk_emitter_end_frame(void);

/* For tests: particle `index` (0 = the oldest in the emitter's window) as written at
 * its birth: born xyz + time, velocity xyz + life, shape (size scale, spin, angle, a
 * random 0..1). False past the window. */
bool sk_emitter_particle(sk_handle_t emitter, int index, float born[4], float motion[4], float shape[4]);
/* For tests: the size curve's keys (in order) and how many. */
int sk_emitter_size_keys(sk_handle_t emitter, float times[8], float values[8]);

#endif // SK_INTERNAL_EMITTER_H
