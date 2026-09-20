#ifndef WGR_INTERNAL_TICK_CLOCK_H
#define WGR_INTERNAL_TICK_CLOCK_H

#include <stdbool.h>

/* Fixed-rate tick scheduling for wgr_set_tick(). Pure logic (no clock, no
 * callbacks) so it can be unit tested; wgr.c feeds it real elapsed time and runs
 * the ticks it asks for. See docs/PLAN-tick.md. */

#define WGR_MAX_TICKS_PER_FRAME 5

typedef struct {
    double step;        /* seconds per tick; 0 = no tick */
    double accumulator; /* elapsed time not yet consumed by ticks */
} wgr_tick_clock_t;

/* hz <= 0 disables ticking. Resets the accumulator. */
void wgr_tick_clock_set_rate(wgr_tick_clock_t *clock, int hz);
bool wgr_tick_clock_enabled(const wgr_tick_clock_t *clock);

/* Add `elapsed` seconds and return how many ticks to run now (at most
 * `max_ticks`). If still a full step or more behind after that, the backlog is
 * dropped (keeping the phase) so a stall can't snowball. */
int wgr_tick_clock_advance(wgr_tick_clock_t *clock, double elapsed, int max_ticks);

/* How far into the next tick we are, 0..1 (0 when no tick is set). */
float wgr_tick_clock_fraction(const wgr_tick_clock_t *clock);

#endif // WGR_INTERNAL_TICK_CLOCK_H
