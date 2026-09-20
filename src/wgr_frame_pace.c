#include "internal/wgr_frame_pace_internal.h"

#include <stddef.h>

void wgri_frame_pace_set_fps(wgri_frame_pace_t *pace, int fps)
{
    if (pace == NULL) {
        return;
    }
    pace->period = fps > 0 ? 1.0 / (double)fps : 0.0;
    pace->next = 0.0;
}

bool wgri_frame_pace_enabled(const wgri_frame_pace_t *pace)
{
    return pace != NULL && pace->period > 0.0;
}

double wgri_frame_pace_wait(const wgri_frame_pace_t *pace, double now)
{
    if (!wgri_frame_pace_enabled(pace) || pace->next <= 0.0) {
        return 0.0; /* unpaced, or the first frame */
    }
    return pace->next - now;
}

void wgri_frame_pace_mark(wgri_frame_pace_t *pace, double now)
{
    if (!wgri_frame_pace_enabled(pace)) {
        return;
    }
    if (pace->next <= 0.0) {
        pace->next = now + pace->period;
        return;
    }
    pace->next += pace->period;
    if (pace->next <= now) {
        pace->next = now + pace->period; /* fell a whole period behind: resync */
    }
}
