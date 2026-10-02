#include "wgr_resource.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_asset_internal.h"
#include "internal/wgr_loader_internal.h"
#include "internal/wgr_resource_internal.h"
#include "wgr_logger.h"

#define KINDS 64 /* the handle's kind field is 6 bits */

static struct {
    wgri_handle_pool_t *pool;
    const wgri_resource_kind_t *kind;
} wgr_resource_kinds[KINDS];

void wgri_resource_register(wgri_handle_pool_t *pool, const wgri_resource_kind_t *kind)
{
    if (pool != NULL && (unsigned)pool->kind < KINDS) {
        wgr_resource_kinds[pool->kind].pool = kind != NULL ? pool : NULL;
        wgr_resource_kinds[pool->kind].kind = kind;
    }
}

/* The header of the record in slot `index` of `pool`. */
static wgri_resource_t *header(const wgri_handle_pool_t *pool, uint16_t index)
{
    return (wgri_resource_t *)((char *)*pool->items + (size_t)index * pool->item_size);
}

static wgri_handle_pool_t *pool_of(wgr_handle_t resource)
{
    const unsigned kind = WGRI_HANDLE_KIND(resource);
    return kind < KINDS ? wgr_resource_kinds[kind].pool : NULL;
}

wgri_resource_t *wgri_resource_get(wgr_handle_t resource)
{
    wgri_handle_pool_t *pool = pool_of(resource);
    uint16_t index = 0;
    return pool != NULL && resource != 0 && wgri_handle_pool_resolve(pool, resource, &index) ? header(pool, index)
                                                                                             : NULL;
}

/* A new record of `kind`, with its defaults and one reference; 0 when the pool is full. */
static wgr_handle_t new_record(wgr_handle_kind_t kind)
{
    wgri_handle_pool_t *pool = (unsigned)kind < KINDS ? wgr_resource_kinds[kind].pool : NULL;
    const wgri_resource_kind_t *desc = pool != NULL ? wgr_resource_kinds[kind].kind : NULL;
    wgr_handle_t handle;
    uint16_t index = 0;

    if (pool == NULL) {
        return 0;
    }
    handle = wgri_handle_pool_alloc(pool);
    if (handle == 0) {
        wgr_logger_error("%s: pool full (%u)", pool->name, (unsigned)pool->max - 1u);
        return 0;
    }
    wgri_handle_pool_resolve(pool, handle, &index);
    memset(header(pool, index), 0, pool->item_size);
    if (desc->init != NULL) {
        desc->init(header(pool, index));
    }
    header(pool, index)->ref_count = 1;
    return handle;
}

wgr_handle_t wgri_resource_add(wgr_handle_kind_t kind)
{
    const wgr_handle_t handle = new_record(kind);
    wgri_resource_t *resource_ptr = wgri_resource_get(handle);
    if (resource_ptr != NULL) {
        resource_ptr->status = WGR_RESOURCE_READY;
    }
    return handle;
}

/* The live resource of `pool` created from `path`, or 0. */
static wgr_handle_t find(const wgri_handle_pool_t *pool, const char *path)
{
    for (uint16_t i = 1; i < pool->capacity; i++) {
        if (pool->occupied[i] && strcmp(header(pool, i)->path, path) == 0) {
            return wgri_handle_pool_handle_from_index(pool, i);
        }
    }
    return 0;
}

wgr_handle_t wgri_resource_create(wgr_handle_kind_t kind, const char *path)
{
    wgri_handle_pool_t *pool = (unsigned)kind < KINDS ? wgr_resource_kinds[kind].pool : NULL;
    const wgri_resource_kind_t *desc = pool != NULL ? wgr_resource_kinds[kind].kind : NULL;
    char key[sizeof(((wgri_resource_t *)0)->path)];
    const bool valid = path != NULL && wgri_asset_normalize_path(path, key, sizeof(key));
    wgr_handle_t handle;
    wgri_resource_t *resource_ptr;

    if (desc == NULL) {
        return 0; /* the kind's module isn't running */
    }
    if (valid && (handle = find(pool, key)) != 0) {
        wgri_resource_retain(handle); /* whatever its status: the same resource */
        return handle;
    }
    handle = new_record(kind);
    resource_ptr = wgri_resource_get(handle);
    if (resource_ptr == NULL) {
        return 0;
    }
    if (!valid) {
        wgr_logger_error("%s: %s isn't a path under the asset root (absolute, a drive, or climbing out with \"..\")",
                         desc->create, path != NULL ? path : "(null)");
        resource_ptr->status = WGR_RESOURCE_FAILED;
        return handle;
    }
    memcpy(resource_ptr->path, key, sizeof(key));
    resource_ptr->status = WGR_RESOURCE_PENDING;
    if (!wgri_asset_load(desc->loader, key, handle)) {
        wgr_logger_error("%s: %s can't be loaded: the asset layer isn't running", desc->create, key);
        resource_ptr->status = WGR_RESOURCE_FAILED;
    }
    return handle;
}

void wgri_resource_retain(wgr_handle_t resource)
{
    wgri_resource_t *resource_ptr = wgri_resource_get(resource);
    if (resource_ptr != NULL) {
        resource_ptr->ref_count++;
    }
}

void wgri_resource_loaded(wgr_handle_t resource, const char *found)
{
    wgri_resource_t *resource_ptr = wgri_resource_get(resource);
    if (resource_ptr != NULL) {
        resource_ptr->status = WGR_RESOURCE_READY;
        snprintf(resource_ptr->found, sizeof(resource_ptr->found), "%s", found != NULL ? found : "");
    }
}

void wgri_resource_failed(wgr_handle_t resource)
{
    wgri_resource_t *resource_ptr = wgri_resource_get(resource);
    if (resource_ptr != NULL) {
        resource_ptr->status = WGR_RESOURCE_FAILED;
    }
}

WGRI_KEEP
wgr_resource_status_t wgr_resource_get_status(wgr_handle_t resource)
{
    const wgri_resource_t *resource_ptr = wgri_resource_get(resource);
    return resource_ptr != NULL ? resource_ptr->status : WGR_RESOURCE_NONE;
}

WGRI_KEEP
const char *wgr_resource_get_path(wgr_handle_t resource)
{
    const wgri_resource_t *resource_ptr = wgri_resource_get(resource);
    return resource_ptr != NULL ? resource_ptr->found : "";
}

WGRI_KEEP
bool wgr_resource_release(wgr_handle_t resource)
{
    wgri_handle_pool_t *pool = pool_of(resource);
    wgri_resource_t *resource_ptr = wgri_resource_get(resource);

    if (resource_ptr == NULL) {
        if (resource != 0) {
            wgr_logger_warn("wgr_resource_release: %u isn't a resource (released already, or an object)",
                            (unsigned)resource);
        }
        return false;
    }
    if (resource_ptr->permanent) {
        return true;
    }
    if (resource_ptr->ref_count > 0) {
        resource_ptr->ref_count--;
    }
    if (resource_ptr->ref_count == 0) {
        if (resource_ptr->status == WGR_RESOURCE_PENDING) {
            wgri_asset_load_cancel(resource);
        }
        wgr_resource_kinds[pool->kind].kind->free(resource_ptr);
        memset(resource_ptr, 0, pool->item_size);
        wgri_handle_pool_free(pool, resource);
    }
    return true;
}
