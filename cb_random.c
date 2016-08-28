#include "cb_random.h"
#include <stdlib.h>


void
cb_random_state_init(struct cb_random_state *rs, uint64_t seed)
{
    rs->r[0] = seed & 0xFFFF;
    rs->r[1] = (seed >> 16) & 0xFFFF;
    rs->r[2] = (seed >> 32) & 0xFFFF;
    rs->r[2] ^= (seed >> 48);
    jrand48(rs->r); //permute once to not return 0 seeds as first value
}


uint64_t
cb_random_next(struct cb_random_state *rs)
{
    uint64_t x    = jrand48(rs->r) & 0xFFFFFFFF;
    uint64_t y    = jrand48(rs->r) & 0xFFFFFFFF;
    uint64_t next = (x << 32) | y;

    return next;
}


uint64_t
cb_random_next_range(struct cb_random_state *rs, uint64_t upper_bound)
{
    uint64_t rand_bound = UINT64_MAX / upper_bound * upper_bound;
    uint64_t next;

    do
    {
        next = cb_random_next(rs);
    } while (next > rand_bound);

    return (next % upper_bound);
}

