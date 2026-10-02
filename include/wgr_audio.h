#ifndef WGR_AUDIO_H
#define WGR_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wgr_resource.h"
#include "wgr_types.h"

/* Audio resource (kind AUDIO) from a source asset (path: MP3, OGG or WAV), loaded on
 * create and released like any resource (wgr_resource.h). A file up to 1 MB is
 * decoded to PCM as it loads; a larger one (music) keeps its encoded bytes and is
 * decoded while it plays. Sound objects reference an Audio by handle (looping music
 * is a Sound too). See docs/ARCHITECTURE.md. */

/* The audio at asset path `path`, loading on create (wgr_resource.h): PENDING at once,
 * then READY, or FAILED in a later frame for a file that is missing, fails to download
 * or won't decode, and FAILED at once for a path outside the asset root or before
 * wgr_run has started the asset layer. 0 only when there's no room for another. A
 * sound playing it before it is READY waits, and plays from the start when it is;
 * FAILED, the sound stays silent. */
wgr_handle_t wgr_audio_create(const char *path);

#ifdef __cplusplus
}
#endif

#endif // WGR_AUDIO_H
