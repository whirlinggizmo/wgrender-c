#include "internal/wgr_frame_pace.h"

#include <stddef.h>

void wgr_frame_pace_set_fps(wgr_frame_pace_t *pace, int fps)
{
    if (pace == NULL) {
        return;
    }
    pace->period = fps > 0 ? 1.0 / (double)fps : 0.0;
    pace->next = 0.0;
}

bool wgr_frame_pace_enabled(const wgr_frame_pace_t *pace)
{
    return pace != NULL && pace->period > 0.0;
}

double wgr_frame_pace_wait(const wgr_frame_pace_t *pace, double now)
{
    if (!wgr_frame_pace_enabled(pace) || pace->next <= 0.0) {
        return 0.0; /* unpaced, or the first frame */
    }
    return pace->next - now;
}

void wgr_frame_pace_mark(wgr_frame_pace_t *pace, double now)
{
    if (!wgr_frame_pace_enabled(pace)) {
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
