#ifndef WGR_INTERNAL_FRAME_PACE_H
#define WGR_INTERNAL_FRAME_PACE_H

#include <stdbool.h>

/* Frame pacing for wgr_set_target_fps(). Pure scheduling logic (no clock, no
 * sleeping) so it can be unit tested; wgr.c supplies the time and decides how to
 * wait (sleep on desktop, skip browser frames on the web).
 *
 * Deadlines advance by a fixed period from the previous deadline, not from when
 * the frame actually ran, so small lateness doesn't make the rate drift. A frame
 * that runs more than a period late resyncs instead of bursting to catch up. */
typedef struct {
    double period; /* seconds per frame; 0 = unpaced */
    double next;   /* when the next frame is due; 0 = not started */
} wgr_frame_pace_t;

/* fps <= 0 disables pacing. Restarts the schedule. */
void wgr_frame_pace_set_fps(wgr_frame_pace_t *pace, int fps);
bool wgr_frame_pace_enabled(const wgr_frame_pace_t *pace);

/* Seconds until the next frame is due at time `now`; <= 0 means it is due. */
double wgr_frame_pace_wait(const wgr_frame_pace_t *pace, double now);

/* Record that a frame ran at `now` and schedule the next one. */
void wgr_frame_pace_mark(wgr_frame_pace_t *pace, double now);

#endif // WGR_INTERNAL_FRAME_PACE_H
