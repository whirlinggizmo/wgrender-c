#ifndef SK_HANDLE_POOL_H
#define SK_HANDLE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sk_handle.h"
#include "sk_types.h"

/* 32-bit handle: [ kind: 6 @ 26 ][ generation: 10 @ 16 ][ index: 16 @ 0 ] */
#define SK_HANDLE_KIND_BITS 6u
#define SK_HANDLE_GENERATION_BITS 10u
#define SK_HANDLE_INDEX_BITS 16u

#define SK_HANDLE_INDEX_MASK ((1u << SK_HANDLE_INDEX_BITS) - 1u)
#define SK_HANDLE_GENERATION_MASK ((1u << SK_HANDLE_GENERATION_BITS) - 1u)
#define SK_HANDLE_KIND_MASK ((1u << SK_HANDLE_KIND_BITS) - 1u)

#define SK_HANDLE_INDEX_SHIFT 0u
#define SK_HANDLE_GENERATION_SHIFT SK_HANDLE_INDEX_BITS
#define SK_HANDLE_KIND_SHIFT (SK_HANDLE_INDEX_BITS + SK_HANDLE_GENERATION_BITS)

#define SK_HANDLE_MAKE(kind, index, generation)                                                    \
    ((sk_handle_t)((((uint32_t)(kind) & SK_HANDLE_KIND_MASK) << SK_HANDLE_KIND_SHIFT) |             \
                   (((uint32_t)(generation) & SK_HANDLE_GENERATION_MASK)                            \
                    << SK_HANDLE_GENERATION_SHIFT) |                                                \
                   (((uint32_t)(index) & SK_HANDLE_INDEX_MASK) << SK_HANDLE_INDEX_SHIFT)))

#define SK_HANDLE_INDEX(handle)                                                                     \
    ((uint16_t)(((handle) >> SK_HANDLE_INDEX_SHIFT) & SK_HANDLE_INDEX_MASK))

#define SK_HANDLE_GENERATION(handle)                                                                \
    ((uint16_t)(((handle) >> SK_HANDLE_GENERATION_SHIFT) & SK_HANDLE_GENERATION_MASK))

#define SK_HANDLE_KIND(handle)                                                                      \
    ((uint8_t)(((handle) >> SK_HANDLE_KIND_SHIFT) & SK_HANDLE_KIND_MASK))

/* Slots a pool can have at most: indices are 16 bits, and 0 is never a slot. */
#define SK_HANDLE_POOL_MAX_SLOTS 65535u

/* Handles into a pool of slots, which starts small and doubles as needed. Index 0 is reserved, so a zero handle is never valid.
 * Freeing a slot bumps its generation, so handles to what it held stop resolving.
 * Freed slots are reused oldest first: churn spreads over all of them, and a stale
 * handle only resolves again after its slot's generation wraps (1023 reuses of that
 * slot). */
typedef struct
{
    sk_handle_kind_t kind;
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
} sk_handle_pool_t;

/* A pool (`name` for log messages): it allocates its bookkeeping and the module's item array
 * (*items, item_size bytes per slot), starting at `initial` slots and doubling when
 * full, up to `max` (at most SK_HANDLE_POOL_MAX_SLOTS). New slots are zeroed. Growing
 * moves *items, so a pointer into it must not be held across an alloc.
 * sk_handle_pool_destroy frees it all. */
bool sk_handle_pool_init(sk_handle_pool_t *pool,
                         sk_handle_kind_t kind,
                         const char *name,
                         void **items,
                         size_t item_size,
                         uint16_t initial,
                         uint16_t max);
void sk_handle_pool_destroy(sk_handle_pool_t *pool);
void sk_handle_pool_reset(sk_handle_pool_t *pool);

sk_handle_t sk_handle_pool_alloc(sk_handle_pool_t *pool);
bool sk_handle_pool_free(sk_handle_pool_t *pool, sk_handle_t handle);

bool sk_handle_pool_resolve(const sk_handle_pool_t *pool, sk_handle_t handle, uint16_t *index_out);
sk_handle_t sk_handle_pool_handle_from_index(const sk_handle_pool_t *pool, uint16_t index);

#endif // SK_HANDLE_POOL_H
