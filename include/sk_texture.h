#ifndef SK_TEXTURE_H
#define SK_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sk_types.h"

sk_handle_t sk_texture_get_default(void);
sk_handle_t sk_texture_create(const char *path);                       /* sync load from disk */
sk_handle_t sk_texture_create_from_memory(const unsigned char *data, int size);
/* Same as the above, but retain a CPU-side alpha mask so the texture can be
 * used for alpha-tested picking (e.g. sk_sprite3d alpha-test). Costs ~w*h bytes. */
sk_handle_t sk_texture_create_pickable(const char *path);
sk_handle_t sk_texture_create_from_memory_pickable(const unsigned char *data, int size);
vec2_t      sk_texture_get_size(sk_handle_t handle);
void        sk_texture_destroy(sk_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif // SK_TEXTURE_H
