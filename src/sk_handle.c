#include "sk_handle.h"

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"

SK_KEEP
sk_handle_kind_t sk_handle_get_kind(sk_handle_t handle)
{
    if (handle == 0) {
        return SK_HANDLE_KIND_NONE;
    }
    return (sk_handle_kind_t)SK_HANDLE_KIND(handle);
}
