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
 * its birth: born xyz + time, velocity xyz + life. False past the window. */
bool sk_emitter_particle(sk_handle_t emitter, int index, float born[4], float motion[4]);

#endif // SK_INTERNAL_EMITTER_H
