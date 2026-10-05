#ifndef WGRI_INTERNAL_RESOURCE_H
#define WGRI_INTERNAL_RESOURCE_H

#include <stdbool.h>

#include "wgr_handle.h"
#include "wgr_resource.h"
#include "internal/wgr_handle_pool_internal.h"

struct wgri_loader;

/* The resource core: what every resource kind shares, so each module keeps only what
 * is its own (wgr_resource.h says what a resource is). A resource module's records
 * start with a wgri_resource_t; it registers its handle pool with a description, and
 * the core does the reference counting, finding a resource by the path it was created
 * from, load on create, the status and path, and release. */

typedef struct {
    int ref_count;
    wgr_resource_status_t status;
    bool permanent;  /* a built-in: never freed, and release does nothing */
    char path[256];  /* the asset path it was created from, which finds it again; "" for none */
    char found[512]; /* the file it was read from, under the asset root; "" until READY */
} wgri_resource_t;

typedef struct {
    const char *create;                 /* the public create's name, for logs: "wgr_texture_create" */
    const struct wgri_loader *loader;   /* loads a created resource's file; NULL: none is */
    void (*init)(void *record);         /* a new record's defaults past the header, or NULL */
    void (*free)(void *record);         /* free what a record holds past the header (GPU objects, memory) */
    /* A kind whose records another thread reads (audio: the mixer) gives the lock it
     * reads them under, which must allow nesting; the core takes it around whatever
     * changes the pool, a record's status or its reference count. NULL: no lock. */
    void (*lock)(void);
    void (*unlock)(void);
} wgri_resource_kind_t;

/* Register the resources in `pool` (whose records start with a wgri_resource_t) for
 * its handle kind; `kind` must outlive the registration. NULL forgets the kind. */
void wgri_resource_register(wgri_handle_pool_t *pool, const wgri_resource_kind_t *kind);

/* The header of a live resource, or NULL (and no warning) for anything else. Don't
 * hold it across a create: the records move when a pool grows. */
wgri_resource_t *wgri_resource_get(wgr_handle_t resource);

/* Load on create: the resource of `kind` at asset path `path`, as wgr_resource.h says.
 * A path already created gives the same handle with one more reference; otherwise a
 * new record, PENDING, whose file the asset layer loads with the kind's loader, or
 * FAILED at once for a path outside the asset root or with the asset layer not
 * running (logged). 0 only when the pool is full. */
wgr_handle_t wgri_resource_create(wgr_handle_kind_t kind, const char *path);

/* The same with a loader other than the kind's own (audio's forced decode or stream,
 * for tests). The path still finds whatever was created from it, however loaded. */
wgr_handle_t wgri_resource_create_with(wgr_handle_kind_t kind, const char *path, const struct wgri_loader *loader);

/* A resource made from numbers: a new record of `kind` with no path, READY, holding
 * one reference; the module fills in the rest. 0 when the pool is full. */
wgr_handle_t wgri_resource_add(wgr_handle_kind_t kind);

void wgri_resource_retain(wgr_handle_t resource);

/* From the asset layer, when the load into `resource` is done: READY, read from `found`
 * (a path under the asset root), or FAILED. */
void wgri_resource_loaded(wgr_handle_t resource, const char *found);
void wgri_resource_failed(wgr_handle_t resource);
/* wgr_resource_set_load_budget's milliseconds, for the asset layer's finishing step. */
float wgri_resource_load_budget(void);

#endif // WGRI_INTERNAL_RESOURCE_H
