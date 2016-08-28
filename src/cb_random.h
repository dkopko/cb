#ifndef _CB_RANDOM_H_
#define _CB_RANDOM_H_

/*
 * The purpose of this module is a data structure that is easy to work with for
 * the generation of random numbers.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


struct cb_random_state
{
    uint16_t r[3];
};


void
cb_random_state_init(struct cb_random_state *rs,
                     uint64_t                seed);

uint64_t
cb_random_next(struct cb_random_state *rs);

uint64_t
cb_random_next_range(struct cb_random_state *rs,
                     uint64_t                upper_bound);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _CB_RANDOM_H_ */
