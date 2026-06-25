#ifndef RL_HANDLE_POOL_H
#define RL_HANDLE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rl.h"
#include "rl_handle.h"

/* 32-bit handle: [ kind: 6 @ 26 ][ generation: 10 @ 16 ][ index: 16 @ 0 ] */
#define RL_HANDLE_KIND_BITS 6u
#define RL_HANDLE_GENERATION_BITS 10u
#define RL_HANDLE_INDEX_BITS 16u

#define RL_HANDLE_INDEX_MASK ((1u << RL_HANDLE_INDEX_BITS) - 1u)
#define RL_HANDLE_GENERATION_MASK ((1u << RL_HANDLE_GENERATION_BITS) - 1u)
#define RL_HANDLE_KIND_MASK ((1u << RL_HANDLE_KIND_BITS) - 1u)

#define RL_HANDLE_INDEX_SHIFT 0u
#define RL_HANDLE_GENERATION_SHIFT RL_HANDLE_INDEX_BITS
#define RL_HANDLE_KIND_SHIFT (RL_HANDLE_INDEX_BITS + RL_HANDLE_GENERATION_BITS)

#define RL_HANDLE_MAKE(kind, index, generation)                                                      \
    ((rl_handle_t)((((uint32_t)(kind) & RL_HANDLE_KIND_MASK) << RL_HANDLE_KIND_SHIFT) |             \
                   (((uint32_t)(generation) & RL_HANDLE_GENERATION_MASK)                            \
                    << RL_HANDLE_GENERATION_SHIFT) |                                               \
                   (((uint32_t)(index) & RL_HANDLE_INDEX_MASK) << RL_HANDLE_INDEX_SHIFT)))

#define RL_HANDLE_INDEX(handle)                                                                        \
    ((uint16_t)(((handle) >> RL_HANDLE_INDEX_SHIFT) & RL_HANDLE_INDEX_MASK))

#define RL_HANDLE_GENERATION(handle)                                                                     \
    ((uint16_t)(((handle) >> RL_HANDLE_GENERATION_SHIFT) & RL_HANDLE_GENERATION_MASK))

#define RL_HANDLE_KIND(handle)                                                                           \
    ((uint8_t)(((handle) >> RL_HANDLE_KIND_SHIFT) & RL_HANDLE_KIND_MASK))

typedef struct
{
    rl_handle_kind_t kind;
    uint16_t max;
    uint16_t next_index;

    uint16_t *free_indices;
    uint16_t free_capacity;
    uint16_t free_count;

    uint16_t *generations;
    unsigned char *occupied;
} rl_handle_pool_t;

void rl_handle_pool_init(rl_handle_pool_t *pool,
                         rl_handle_kind_t kind,
                         uint16_t max,
                         uint16_t *free_indices,
                         uint16_t free_capacity,
                         uint16_t *generations,
                         unsigned char *occupied);
void rl_handle_pool_reset(rl_handle_pool_t *pool);

rl_handle_t rl_handle_pool_alloc(rl_handle_pool_t *pool);
bool rl_handle_pool_free(rl_handle_pool_t *pool, rl_handle_t handle);

bool rl_handle_pool_resolve(const rl_handle_pool_t *pool, rl_handle_t handle, uint16_t *index_out);
rl_handle_t rl_handle_pool_handle_from_index(const rl_handle_pool_t *pool, uint16_t index);

#endif // RL_HANDLE_POOL_H
