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
void        sk_audio_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_AUDIO_H
