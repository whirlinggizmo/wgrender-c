#ifndef SK_AUDIO_H
#define SK_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

/* Audio resource (kind AUDIO): decoded PCM loaded from a source asset (path),
 * reference-counted and deduplicated. Sound and Music objects reference an Audio
 * by handle. See docs/ARCHITECTURE.md. */

sk_handle_t sk_audio_create(const char *path);
/* Drop this handle's reference to the resource. Resources are shared and
 * reference counted (loading the same path again returns the same handle, with
 * one more reference), so a resource is freed when its last reference goes, not
 * when you call this. Objects hold their own references, so handing a resource
 * to one and releasing it right away is the normal pattern. */
void        sk_audio_release(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_AUDIO_H
