#include "wgr_resource.h"

#include <stddef.h>

#include "internal/exports_internal.h"
#include "internal/wgr_resource_internal.h"

#define KINDS 64 /* the handle's kind field is 6 bits */

static wgri_resource_status_fn wgr_resource_status[KINDS];

void wgri_resource_register_status(wgr_handle_kind_t kind, wgri_resource_status_fn status)
{
    if ((unsigned)kind < KINDS) {
        wgr_resource_status[kind] = status;
    }
}

WGRI_KEEP
wgr_resource_status_t wgr_resource_get_status(wgr_handle_t resource)
{
    const wgr_handle_kind_t kind = wgr_handle_get_kind(resource);
    const wgri_resource_status_fn status = (unsigned)kind < KINDS ? wgr_resource_status[kind] : NULL;
    return status != NULL ? status(resource) : WGR_RESOURCE_NONE;
}
