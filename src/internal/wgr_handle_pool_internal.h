#ifndef WGR_HANDLE_POOL_H
#define WGR_HANDLE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wgr_handle.h"
#include "wgr_types.h"

/* 32-bit handle: [ kind: 6 @ 26 ][ generation: 10 @ 16 ][ index: 16 @ 0 ] */
#define WGR_HANDLE_KIND_BITS 6u
#define WGR_HANDLE_GENERATION_BITS 10u
#define WGR_HANDLE_INDEX_BITS 16u

#define WGR_HANDLE_INDEX_MASK ((1u << WGR_HANDLE_INDEX_BITS) - 1u)
#define WGR_HANDLE_GENERATION_MASK ((1u << WGR_HANDLE_GENERATION_BITS) - 1u)
#define WGR_HANDLE_KIND_MASK ((1u << WGR_HANDLE_KIND_BITS) - 1u)

#define WGR_HANDLE_INDEX_SHIFT 0u
#define WGR_HANDLE_GENERATION_SHIFT WGR_HANDLE_INDEX_BITS
#define WGR_HANDLE_KIND_SHIFT (WGR_HANDLE_INDEX_BITS + WGR_HANDLE_GENERATION_BITS)

#define WGR_HANDLE_MAKE(kind, index, generation)                                                    \
    ((wgr_handle_t)((((uint32_t)(kind) & WGR_HANDLE_KIND_MASK) << WGR_HANDLE_KIND_SHIFT) |             \
                   (((uint32_t)(generation) & WGR_HANDLE_GENERATION_MASK)                            \
                    << WGR_HANDLE_GENERATION_SHIFT) |                                                \
                   (((uint32_t)(index) & WGR_HANDLE_INDEX_MASK) << WGR_HANDLE_INDEX_SHIFT)))

#define WGR_HANDLE_INDEX(handle)                                                                     \
    ((uint16_t)(((handle) >> WGR_HANDLE_INDEX_SHIFT) & WGR_HANDLE_INDEX_MASK))

#define WGR_HANDLE_GENERATION(handle)                                                                \
    ((uint16_t)(((handle) >> WGR_HANDLE_GENERATION_SHIFT) & WGR_HANDLE_GENERATION_MASK))

#define WGR_HANDLE_KIND(handle)                                                                      \
    ((uint8_t)(((handle) >> WGR_HANDLE_KIND_SHIFT) & WGR_HANDLE_KIND_MASK))

/* Slots a pool can have at most: indices are 16 bits, and 0 is never a slot. */
#define WGR_HANDLE_POOL_MAX_SLOTS 65535u

/* Handles into a pool of slots, which starts small and doubles as needed. Index 0 is reserved, so a zero handle is never valid.
 * Freeing a slot bumps its generation, so handles to what it held stop resolving.
 * Freed slots are reused oldest first: churn spreads over all of them, and a stale
 * handle only resolves again after its slot's generation wraps (1023 reuses of that
 * slot). */
typedef struct
{
    wgr_handle_kind_t kind;
    uint16_t capacity; /* slots now, index 0 included */
    uint16_t max;      /* capacity can grow to this */
    uint16_t next_index;

    uint16_t *free_indices; /* ring of `capacity`: every free slot, the oldest first */
    uint16_t free_head;
    uint16_t free_count;

    uint16_t *generations;
    unsigned char *occupied;

    /* the module's item array, grown and zeroed with the slots */
    void **items;
    size_t item_size;
    const char *name; /* for log messages */
} wgr_handle_pool_t;

/* A pool (`name` for log messages): it allocates its bookkeeping and the module's item array
 * (*items, item_size bytes per slot), starting at `initial` slots and doubling when
 * full, up to `max` (at most WGR_HANDLE_POOL_MAX_SLOTS). New slots are zeroed. Growing
 * moves *items, so a pointer into it must not be held across an alloc.
 * wgr_handle_pool_destroy frees it all. */
bool wgr_handle_pool_init(wgr_handle_pool_t *pool,
                         wgr_handle_kind_t kind,
                         const char *name,
                         void **items,
                         size_t item_size,
                         uint16_t initial,
                         uint16_t max);
void wgr_handle_pool_destroy(wgr_handle_pool_t *pool);
void wgr_handle_pool_reset(wgr_handle_pool_t *pool);

wgr_handle_t wgr_handle_pool_alloc(wgr_handle_pool_t *pool);
bool wgr_handle_pool_free(wgr_handle_pool_t *pool, wgr_handle_t handle);

bool wgr_handle_pool_resolve(const wgr_handle_pool_t *pool, wgr_handle_t handle, uint16_t *index_out);
wgr_handle_t wgr_handle_pool_handle_from_index(const wgr_handle_pool_t *pool, uint16_t index);

#endif // WGR_HANDLE_POOL_H
