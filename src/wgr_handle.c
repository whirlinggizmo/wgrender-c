#include "wgr_handle.h"

#include "internal/exports.h"
#include "internal/wgr_handle_pool.h"

WGR_KEEP
wgr_handle_kind_t wgr_handle_get_kind(wgr_handle_t handle)
{
    if (handle == 0) {
        return WGR_HANDLE_KIND_NONE;
    }
    return (wgr_handle_kind_t)WGR_HANDLE_KIND(handle);
}
