#ifndef WGRI_INTERNAL_RESOURCE_H
#define WGRI_INTERNAL_RESOURCE_H

#include "wgr_handle.h"
#include "wgr_resource.h"

/* A resource kind's status, for wgr_resource_get_status: NONE for a handle that isn't
 * one of its resources. */
typedef wgr_resource_status_t (*wgri_resource_status_fn)(wgr_handle_t resource);

/* Register (or with NULL, forget) the status getter for resources of `kind`. Each
 * resource module registers its own when it starts, so the core never names one. */
void wgri_resource_register_status(wgr_handle_kind_t kind, wgri_resource_status_fn status);

#endif // WGRI_INTERNAL_RESOURCE_H
