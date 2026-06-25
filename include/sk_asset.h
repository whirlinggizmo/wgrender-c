#ifndef SK_ASSET_H
#define SK_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "sk_types.h"

/* Asynchronous file loading built on sokol_fetch (local files on desktop, XHR
 * on web). The success callback delivers the loaded bytes; the consumer turns
 * them into a texture/sound/model/etc. The bytes are owned by libsk and freed
 * after the callback returns. */

typedef void (*sk_asset_loaded_fn)(const char *path, const unsigned char *data,
                                   int size, void *user_data);
typedef void (*sk_asset_failed_fn)(const char *path, void *user_data);

/* Begin loading `path`. Returns true if the request was queued. Callbacks fire
 * during sk_asset_tick() (driven each frame by the runtime), on the main
 * thread. */
bool sk_asset_load_async(const char *path,
                         sk_asset_loaded_fn on_loaded,
                         sk_asset_failed_fn on_failed,
                         void *user_data);

#ifdef __cplusplus
}
#endif

#endif // SK_ASSET_H
