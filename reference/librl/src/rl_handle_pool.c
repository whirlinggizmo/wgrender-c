#include "internal/rl_handle_pool.h"

#include <string.h>

void rl_handle_pool_init(rl_handle_pool_t *pool,
                         rl_handle_kind_t kind,
                         uint16_t max,
                         uint16_t *free_indices,
                         uint16_t free_capacity,
                         uint16_t *generations,
                         unsigned char *occupied)
{
    if (pool == NULL) {
        return;
    }

    pool->kind = kind;
    pool->max = max;
    pool->next_index = 1; // 0 is always reserved as invalid
    pool->free_indices = free_indices;
    pool->free_capacity = free_capacity;
    pool->free_count = 0;
    pool->generations = generations;
    pool->occupied = occupied;

    if (pool->generations != NULL) {
        memset(pool->generations, 0, sizeof(uint16_t) * max);
    }
    if (pool->occupied != NULL) {
        memset(pool->occupied, 0, sizeof(unsigned char) * max);
    }
}

void rl_handle_pool_reset(rl_handle_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    pool->next_index = 1;
    pool->free_count = 0;

    if (pool->generations != NULL) {
        memset(pool->generations, 0, sizeof(uint16_t) * pool->max);
    }
    if (pool->occupied != NULL) {
        memset(pool->occupied, 0, sizeof(unsigned char) * pool->max);
    }
}

static uint16_t find_free_slot_index(rl_handle_pool_t *pool)
{
    if (pool->free_count > 0) {
        pool->free_count--;
        return pool->free_indices[pool->free_count];
    }

    for (uint16_t i = pool->next_index; i < pool->max; i++) {
        if (!pool->occupied[i]) {
            pool->next_index = (uint16_t)(i + 1u);
            return i;
        }
    }
    for (uint16_t i = 1; i < pool->next_index; i++) {
        if (!pool->occupied[i]) {
            pool->next_index = (uint16_t)(i + 1u);
            return i;
        }
    }

    return 0;
}

static uint16_t bump_slot_generation(uint16_t generation)
{
    uint16_t next_generation = (uint16_t)((generation + 1u) & RL_HANDLE_GENERATION_MASK);
    if (next_generation == 0) {
        next_generation = 1;
    }
    return next_generation;
}

rl_handle_t rl_handle_pool_alloc(rl_handle_pool_t *pool)
{
    uint16_t index = 0;
    uint16_t generation = 0;

    if (pool == NULL || pool->kind == RL_HANDLE_KIND_NONE) {
        return 0;
    }

    index = find_free_slot_index(pool);
    if (index == 0 || index >= pool->max) {
        return 0;
    }

    generation = pool->generations[index];
    if (generation == 0) {
        generation = 1;
        pool->generations[index] = generation;
    }

    pool->occupied[index] = 1;
    return RL_HANDLE_MAKE(pool->kind, index, generation);
}

bool rl_handle_pool_free(rl_handle_pool_t *pool, rl_handle_t handle)
{
    uint16_t index = 0;
    uint16_t generation = 0;

    if (pool == NULL) {
        return false;
    }

    index = RL_HANDLE_INDEX(handle);
    generation = RL_HANDLE_GENERATION(handle);

    if (RL_HANDLE_KIND(handle) != (uint8_t)pool->kind) {
        return false;
    }
    if (index == 0 || index >= pool->max) {
        return false;
    }
    if (!pool->occupied[index]) {
        return false;
    }
    if (pool->generations[index] != generation) {
        return false;
    }

    pool->occupied[index] = 0;
    pool->generations[index] = bump_slot_generation(generation);

    if (pool->free_count < pool->free_capacity) {
        pool->free_indices[pool->free_count] = index;
        pool->free_count++;
    }

    return true;
}

bool rl_handle_pool_resolve(const rl_handle_pool_t *pool, rl_handle_t handle, uint16_t *index_out)
{
    uint16_t index = 0;
    uint16_t generation = 0;

    if (pool == NULL) {
        return false;
    }

    index = RL_HANDLE_INDEX(handle);
    generation = RL_HANDLE_GENERATION(handle);

    if (RL_HANDLE_KIND(handle) != (uint8_t)pool->kind) {
        return false;
    }
    if (index == 0 || index >= pool->max) {
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

rl_handle_t rl_handle_pool_handle_from_index(const rl_handle_pool_t *pool, uint16_t index)
{
    if (pool == NULL || pool->kind == RL_HANDLE_KIND_NONE) {
        return 0;
    }
    if (index == 0 || index >= pool->max) {
        return 0;
    }
    if (!pool->occupied[index]) {
        return 0;
    }

    return RL_HANDLE_MAKE(pool->kind, index, pool->generations[index]);
}
