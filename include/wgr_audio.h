#ifndef WGR_AUDIO_H
#define WGR_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_types.h"

/* Audio resource (kind AUDIO) from a source asset (path: MP3, OGG or WAV),
 * reference-counted and deduplicated. A file up to 1 MB is decoded to PCM when it is
 * created; a larger one (music) keeps its encoded bytes and is decoded while it plays.
 * Sound objects reference an Audio by handle (looping music is a Sound too). See
 * docs/ARCHITECTURE.md. */

wgr_handle_t wgr_audio_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        wgr_audio_release(wgr_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // WGR_AUDIO_H
