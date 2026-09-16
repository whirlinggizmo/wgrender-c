#ifndef SK_INTERNAL_FRAME_PACE_H
#define SK_INTERNAL_FRAME_PACE_H

#include <stdbool.h>

/* Frame pacing for sk_set_target_fps(). Pure scheduling logic (no clock, no
 * sleeping) so it can be unit tested; sk.c supplies the time and decides how to
 * wait (sleep on desktop, skip browser frames on the web).
 *
 * Deadlines advance by a fixed period from the previous deadline, not from when
 * the frame actually ran, so small lateness doesn't make the rate drift. A frame
 * that runs more than a period late resyncs instead of bursting to catch up. */
typedef struct {
    double period; /* seconds per frame; 0 = unpaced */
    double next;   /* when the next frame is due; 0 = not started */
} sk_frame_pace_t;

/* fps <= 0 disables pacing. Restarts the schedule. */
void sk_frame_pace_set_fps(sk_frame_pace_t *pace, int fps);
bool sk_frame_pace_enabled(const sk_frame_pace_t *pace);

/* Seconds until the next frame is due at time `now`; <= 0 means it is due. */
double sk_frame_pace_wait(const sk_frame_pace_t *pace, double now);

/* Record that a frame ran at `now` and schedule the next one. */
void sk_frame_pace_mark(sk_frame_pace_t *pace, double now);

#endif // SK_INTERNAL_FRAME_PACE_H
