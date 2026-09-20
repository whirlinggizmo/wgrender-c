#ifndef WGR_PICK_H
#define WGR_PICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Picking one object. To pick among many, add them to a scene and use
 * wgr_scene_pick, which returns the nearest hit (2D members first).
 *
 * (x, y) is a screen point in logical pixels, e.g. the mouse. 2D objects
 * (sprite2d, text2d) are hit-tested in screen space; 3D objects (model, shape,
 * sprite3d, text3d) with a ray through `camera` (0 = the active camera). Objects
 * that are hidden or not pickable (wgr_<kind>_set_pickable) are never hit. */
wgr_pick_result_t wgr_pick_object(wgr_handle_t object, wgr_handle_t camera, float x, float y);

/* Work done by picks since the last reset, for debugging: broadphase = bounding
 * box tests, narrowphase = exact tests against triangles, quads or rectangles. */
typedef struct {
    int broadphase_tests;
    int broadphase_rejects;
    int narrowphase_tests;
    int narrowphase_hits;
} wgr_pick_stats_t;

wgr_pick_stats_t wgr_pick_get_stats(void);
void            wgr_pick_reset_stats(void);

#ifdef __cplusplus
}
#endif

#endif // WGR_PICK_H
