#include "internal/wgr_handle_pool.h"

#include <stdlib.h>
#include <string.h>

#include "wgr_logger.h"

/* Grow the pool's slots, bookkeeping and items to `capacity`, zeroing the new
 * slots. Only called with no free slot left, so the free ring is empty. */
static bool grow(wgr_handle_pool_t *pool, uint16_t capacity)
{
    const size_t old_capacity = pool->capacity;
    uint16_t *generations = realloc(pool->generations, sizeof(uint16_t) * capacity);
    unsigned char *occupied;
    uint16_t *free_indices;
    unsigned char *items;

    if (generations == NULL) {
        return false;
    }
    pool->generations = generations;
    occupied = realloc(pool->occupied, capacity);
    if (occupied == NULL) {
        return false;
    }
    pool->occupied = occupied;
    free_indices = realloc(pool->free_indices, sizeof(uint16_t) * capacity);
    if (free_indices == NULL) {
        return false;
    }
    pool->free_indices = free_indices;
    items = realloc(*pool->items, pool->item_size * capacity);
    if (items == NULL) {
        return false;
    }
    *pool->items = items;

    memset(generations + old_capacity, 0, sizeof(uint16_t) * (capacity - old_capacity));
    memset(occupied + old_capacity, 0, capacity - old_capacity);
    memset(items + pool->item_size * old_capacity, 0, pool->item_size * (capacity - old_capacity));
    pool->free_head = 0;
    pool->capacity = capacity;
    return true;
}

bool wgr_handle_pool_init(wgr_handle_pool_t *pool,
                         wgr_handle_kind_t kind,
                         const char *name,
                         void **items,
                         size_t item_size,
                         uint16_t initial,
                         uint16_t max)
                         {
    if (pool == NULL || items == NULL || item_size == 0) {
        return false;
    }
    if (max < 2) {
        max = 2;
    }
    if (initial < 2) {
        initial = 2; /* slot 0 plus one */
    }
    if (initial > max) {
        initial = max;
    }
    *items = NULL;
    *pool = (wgr_handle_pool_t){
        .kind = kind,
        .max = max,
        .next_index = 1,
        .items = items,
        .item_size = item_size,
        .name = name,
    };
    if (!grow(pool, initial)) {
        wgr_handle_pool_destroy(pool);
        return false;
    }
    return true;
}

void wgr_handle_pool_destroy(wgr_handle_pool_t *pool)
{
    if (pool == NULL || pool->items == NULL) {
        return;
    }
    free(pool->generations);
    free(pool->occupied);
    free(pool->free_indices);
    free(*pool->items);
    *pool->items = NULL;
    *pool = (wgr_handle_pool_t){0};
}

void wgr_handle_pool_reset(wgr_handle_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    pool->next_index = 1; // 0 is always reserved as invalid
    pool->free_head = 0;
    pool->free_count = 0;

    if (pool->generations != NULL) {
        memset(pool->generations, 0, sizeof(uint16_t) * pool->capacity);
    }
    if (pool->occupied != NULL) {
        memset(pool->occupied, 0, sizeof(unsigned char) * pool->capacity);
    }
    if (pool->items != NULL && *pool->items != NULL) {
        memset(*pool->items, 0, pool->item_size * pool->capacity);
    }
}

static uint16_t find_free_slot_index(wgr_handle_pool_t *pool)
{
    if (pool->free_count > 0) {
        const uint16_t index = pool->free_indices[pool->free_head];
        pool->free_head = (uint16_t)((pool->free_head + 1u) % pool->capacity);
        pool->free_count--;
        return index;
    }

    /* then slots never used (skipping any reserved by hand) */
    for (uint16_t i = pool->next_index; i < pool->capacity; i++) {
        if (!pool->occupied[i]) {
            pool->next_index = (uint16_t)(i + 1u);
            return i;
        }
    }

    /* every slot in use: double the pool */
    if (pool->capacity < pool->max) {
        const uint16_t old_capacity = pool->capacity;
        const uint32_t doubled = (uint32_t)old_capacity * 2u;
        const uint16_t capacity = (uint16_t)(doubled < pool->max ? doubled : pool->max);
        if (!grow(pool, capacity)) {
            log_error("%s: out of memory growing to %u slots", pool->name, (unsigned)capacity);
            return 0;
        }
        log_debug("%s: grown to %u slots", pool->name, (unsigned)capacity);
        pool->next_index = (uint16_t)(old_capacity + 1u);
        return old_capacity;
    }
    return 0;
}

static uint16_t bump_slot_generation(uint16_t generation)
{
    uint16_t next_generation = (uint16_t)((generation + 1u) & WGR_HANDLE_GENERATION_MASK);
    if (next_generation == 0) {
        next_generation = 1;
    }
    return next_generation;
}

wgr_handle_t wgr_handle_pool_alloc(wgr_handle_pool_t *pool)
{
    uint16_t index = 0;
    uint16_t generation = 0;

    if (pool == NULL || pool->kind == WGR_HANDLE_KIND_NONE) {
        return 0;
    }

    index = find_free_slot_index(pool);
    if (index == 0 || index >= pool->capacity) {
        return 0;
    }

    generation = pool->generations[index];
    if (generation == 0) {
        generation = 1;
        pool->generations[index] = generation;
    }

    pool->occupied[index] = 1;
    return WGR_HANDLE_MAKE(pool->kind, index, generation);
}

bool wgr_handle_pool_free(wgr_handle_pool_t *pool, wgr_handle_t handle)
{
    uint16_t index = 0;

    if (!wgr_handle_pool_resolve(pool, handle, &index)) {
        return false;
    }

    pool->occupied[index] = 0;
    pool->generations[index] = bump_slot_generation(pool->generations[index]);

    pool->free_indices[(pool->free_head + pool->free_count) % pool->capacity] = index;
    pool->free_count++;

    return true;
}

bool wgr_handle_pool_resolve(const wgr_handle_pool_t *pool, wgr_handle_t handle, uint16_t *index_out)
{
    uint16_t index = 0;
    uint16_t generation = 0;

    if (pool == NULL) {
        return false;
    }

    index = WGR_HANDLE_INDEX(handle);
    generation = WGR_HANDLE_GENERATION(handle);

    if (WGR_HANDLE_KIND(handle) != (uint8_t)pool->kind) {
        return false;
    }
    if (index == 0 || index >= pool->capacity) {
        return false;
    }
    if (!pool->occupied[index]) {
        return false;
    }
    if (pool->generations[index] != generation) {
        return false;
    }

    if (index_out != NULL) {
        *index_out = index;
    }
    return true;
}

wgr_handle_t wgr_handle_pool_handle_from_index(const wgr_handle_pool_t *pool, uint16_t index)
{
    if (pool == NULL || pool->kind == WGR_HANDLE_KIND_NONE) {
        return 0;
    }
    if (index == 0 || index >= pool->capacity) {
        return 0;
    }
    if (!pool->occupied[index]) {
        return 0;
    }

    return WGR_HANDLE_MAKE(pool->kind, index, pool->generations[index]);
}
