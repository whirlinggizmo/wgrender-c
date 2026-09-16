#include <stdint.h>

#include "internal/sk_handle_pool.h"
#include "test.h"
#include "tests.h"

enum { POOL_MAX = 4 }; /* index 0 is reserved, so the pool holds 3 handles */

typedef struct {
    sk_handle_pool_t pool;
    uint16_t free_indices[POOL_MAX];
    uint16_t generations[POOL_MAX];
    unsigned char occupied[POOL_MAX];
} test_pool_t;

static void init_pool(test_pool_t *p)
{
    sk_handle_pool_init(&p->pool, SK_HANDLE_KIND_TEXTURE, POOL_MAX, p->free_indices, POOL_MAX,
                        p->generations, p->occupied);
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
    sk_handle_t wrong_kind = SK_HANDLE_MAKE(SK_HANDLE_KIND_COLOR, SK_HANDLE_INDEX(h2), SK_HANDLE_GENERATION(h2));
    CHECK(!sk_handle_pool_resolve(&p.pool, wrong_kind, &index));
    CHECK(!sk_handle_pool_free(&p.pool, wrong_kind));

    /* reserved and out-of-range indices */
    CHECK(!sk_handle_pool_resolve(&p.pool, 0, &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, 0, 1), &index));
    CHECK(!sk_handle_pool_resolve(&p.pool, SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, POOL_MAX, 1), &index));
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
}
