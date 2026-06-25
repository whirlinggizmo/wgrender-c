#include "rl_handle.h"

#include "internal/exports.h"
#include "internal/rl_handle_pool.h"

RL_KEEP
rl_handle_kind_t rl_handle_get_kind(rl_handle_t handle)
{
    if (handle == 0) {
        return RL_HANDLE_KIND_NONE;
    }
    return (rl_handle_kind_t)RL_HANDLE_KIND(handle);
}
