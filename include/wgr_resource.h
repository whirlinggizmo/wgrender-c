#ifndef WGR_RESOURCE_H
#define WGR_RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Resources: the shared, reference counted data an object uses (a texture, a mesh, an
 * audio clip, a font, an environment, a shader). What every resource has in common is
 * here; each kind's own calls are in its header (wgr_texture.h, ...).
 *
 * A resource loads on create: wgr_<kind>_create(path) takes an asset path (relative to
 * the asset root, the same file on every platform) and returns the handle at once,
 * PENDING. The file is made local (from the cache, downloaded, or through a redirect),
 * prepared on a worker and finished on the main thread, and the handle turns READY or
 * FAILED in a later frame, at the start of it, so a frame callback that checks sees
 * each change once, in order. Nothing is called back. Creating the same path again
 * gives the same handle, with one more reference, whatever its status; releasing the
 * last reference of one still loading cancels the load. A create returns 0 only when
 * there's no room for another resource of its kind. A resource made from nothing but
 * numbers (a render target, a generated mesh) is READY from the start.
 *
 * Objects take a resource in any status and do the right thing until it is READY (each
 * kind's header says what), so a program can create everything at once and never
 * wait; the status is for what a program wants to show, such as a loading screen. */

typedef enum
{
    WGR_RESOURCE_NONE = 0,    /* not a loadable resource: an object, a stale handle, 0 */
    WGR_RESOURCE_PENDING = 1, /* its file is being made local, prepared or finished */
    WGR_RESOURCE_READY = 2,
    WGR_RESOURCE_FAILED = 3,  /* the path, the fetch, the file or the decode failed (logged why) */
} wgr_resource_status_t;

/* PENDING, READY or FAILED for a resource of any kind; NONE for anything else. */
wgr_resource_status_t wgr_resource_get_status(wgr_handle_t resource);

#ifdef __cplusplus
}
#endif

#endif // WGR_RESOURCE_H
