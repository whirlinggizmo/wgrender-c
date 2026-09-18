#include <stdint.h>

#include "internal/sk_handle_pool.h"
#include "test.h"
#include "tests.h"

enum { POOL_MAX = 4 }; /* index 0 is reserved, so the pool holds 3 handles */

typedef struct {
    sk_handle_pool_t pool;
    int *items;
} test_pool_t;

/* A pool capped at POOL_MAX slots (it starts there, so it never grows). */
static void init_pool(test_pool_t *p)
{
    CHECK(sk_handle_pool_init(&p->pool, SK_HANDLE_KIND_TEXTURE, "test", (void **)&p->items, sizeof(int), POOL_MAX,
                              POOL_MAX));
}

void test_handle_pool(void)
{
    test_pool_t p;
    uint16_t index = 0;
    init_pool(&p);

    sk_handle_t h1 = sk_handle_pool_alloc(&p.pool);
    sk_handle_t h2 = sk_handle_pool_alloc(&p.pool);
    sk_handle_t h3 = sk_handle_pool_alloc(&p.pool);
    CHECK(h1 != 0 && h2 != 0 && h3 != 0);
    CHECK(sk_handle_pool_alloc(&p.pool) == 0); /* full */

    CHECK(SK_HANDLE_KIND(h2) == SK_HANDLE_KIND_TEXTURE);
    CHECK(SK_HANDLE_INDEX(h1) != SK_HANDLE_INDEX(h2) && SK_HANDLE_INDEX(h2) != SK_HANDLE_INDEX(h3));
    CHECK(SK_HANDLE_INDEX(h1) != 0 && SK_HANDLE_INDEX(h2) != 0 && SK_HANDLE_INDEX(h3) != 0);

    CHECK(sk_handle_pool_resolve(&p.pool, h2, &index));
    CHECK(index == SK_HANDLE_INDEX(h2));
    CHECK(sk_handle_pool_handle_from_index(&p.pool, index) == h2);
    CHECK(sk_handle_pool_resolve(&p.pool, h1, NULL)); /* index_out is optional */

    /* same slot, wrong kind */
    sk_handle_t wrong_kind = SK_HANDLE_MAKE(SK_HANDLE_KIND_MESH, SK_HANDLE_INDEX(h2), SK_HANDLE_GENERATION(h2));
    CHECK(!sk_handle_pool_resolve(&p.pool, wrong_kind, &index));
    CHECK(!sk_handle_pool_free(&p.pool, wrong_kind));

    /* reserved and out-of-range indices */
    CHECK(!sk_handle_pool_resolve(&p.pool, 0, &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, 0, 1), &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, POOL_MAX, 1), &index));
    sk_handle_pool_destroy(&p.pool);
}

void test_handle_pool_reuse(void)
{
    test_pool_t p;
    uint16_t index = 0;
    init_pool(&p);

    sk_handle_t h1 = sk_handle_pool_alloc(&p.pool);
    sk_handle_t h2 = sk_handle_pool_alloc(&p.pool);

    CHECK(sk_handle_pool_free(&p.pool, h2));
    CHECK(!sk_handle_pool_resolve(&p.pool, h2, &index));
    CHECK(!sk_handle_pool_free(&p.pool, h2)); /* double free */

    /* the freed slot is reused with a new generation; the stale handle stays dead */
    sk_handle_t h2_new = sk_handle_pool_alloc(&p.pool);
    CHECK(SK_HANDLE_INDEX(h2_new) == SK_HANDLE_INDEX(h2));
    CHECK(SK_HANDLE_GENERATION(h2_new) != SK_HANDLE_GENERATION(h2));
    CHECK(sk_handle_pool_resolve(&p.pool, h2_new, &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, h2, &index));

    /* generations wrap past the 10-bit maximum without ever becoming 0 */
    sk_handle_t h = h2_new;
    for (int i = 0; i < 2000 && SK_HANDLE_GENERATION(h) != SK_HANDLE_GENERATION_MASK; i++) {
        sk_handle_pool_free(&p.pool, h);
        h = sk_handle_pool_alloc(&p.pool);
    }
    CHECK(SK_HANDLE_GENERATION(h) == SK_HANDLE_GENERATION_MASK);
    sk_handle_pool_free(&p.pool, h);
    h = sk_handle_pool_alloc(&p.pool);
    CHECK(SK_HANDLE_INDEX(h) == SK_HANDLE_INDEX(h2));
    CHECK(SK_HANDLE_GENERATION(h) == 1);

    /* reset invalidates everything and starts over at index 1, generation 1 */
    sk_handle_pool_reset(&p.pool);
    CHECK(!sk_handle_pool_resolve(&p.pool, h1, &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, h, &index));
    h1 = sk_handle_pool_alloc(&p.pool);
    CHECK(SK_HANDLE_INDEX(h1) == 1);
    CHECK(SK_HANDLE_GENERATION(h1) == 1);
    sk_handle_pool_destroy(&p.pool);
}

void test_handle_pool_fifo(void)
{
    test_pool_t p;
    init_pool(&p);

    sk_handle_t h1 = sk_handle_pool_alloc(&p.pool);
    sk_handle_t h2 = sk_handle_pool_alloc(&p.pool);
    sk_handle_t h3 = sk_handle_pool_alloc(&p.pool);

    /* freed slots come back oldest first, so churn spreads over all of them rather
     * than wrapping one slot's generation */
    sk_handle_pool_free(&p.pool, h2);
    sk_handle_pool_free(&p.pool, h1);
    sk_handle_pool_free(&p.pool, h3);
    CHECK(SK_HANDLE_INDEX(sk_handle_pool_alloc(&p.pool)) == SK_HANDLE_INDEX(h2));
    sk_handle_t again = sk_handle_pool_alloc(&p.pool);
    CHECK(SK_HANDLE_INDEX(again) == SK_HANDLE_INDEX(h1));
    sk_handle_pool_free(&p.pool, again);
    CHECK(SK_HANDLE_INDEX(sk_handle_pool_alloc(&p.pool)) == SK_HANDLE_INDEX(h3));
    CHECK(SK_HANDLE_INDEX(sk_handle_pool_alloc(&p.pool)) == SK_HANDLE_INDEX(h1));
    sk_handle_pool_destroy(&p.pool);
}

typedef struct {
    int value;
    char pad[20];
} test_item_t;

void test_handle_pool_growable(void)
{
    enum { MAX = 40, COUNT = MAX - 1 }; /* slot 0 is reserved */
    sk_handle_pool_t pool;
    test_item_t *items = NULL;
    sk_handle_t handles[COUNT];
    uint16_t index = 0;
    bool all_resolve = true;

    CHECK(sk_handle_pool_init(&pool, SK_HANDLE_KIND_SPRITE3D, "test", (void **)&items, sizeof(test_item_t),
                                       4, MAX));
    CHECK(items != NULL && pool.capacity == 4);

    /* past the initial slots it doubles (4, 8, 16, 32, then the most, 40); handles
     * and item contents survive every move */
    for (int i = 0; i < COUNT; i++) {
        handles[i] = sk_handle_pool_alloc(&pool);
        CHECK(sk_handle_pool_resolve(&pool, handles[i], &index));
        CHECK(items[index].value == 0); /* new slots are zeroed */
        items[index].value = 1000 + i;
    }
    CHECK(pool.capacity == MAX);
    CHECK(sk_handle_pool_alloc(&pool) == 0); /* full at the most */
    for (int i = 0; i < COUNT; i++) {
        all_resolve = all_resolve && sk_handle_pool_resolve(&pool, handles[i], &index) &&
                      items[index].value == 1000 + i;
    }
    CHECK(all_resolve);

    /* freeing makes room without growing further */
    CHECK(sk_handle_pool_free(&pool, handles[5]));
    CHECK(!sk_handle_pool_resolve(&pool, handles[5], NULL));
    handles[5] = sk_handle_pool_alloc(&pool);
    CHECK(handles[5] != 0 && pool.capacity == MAX);

    sk_handle_pool_destroy(&pool);
    CHECK(items == NULL);
    CHECK(!sk_handle_pool_resolve(&pool, handles[0], NULL));
}
