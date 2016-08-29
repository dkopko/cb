/* Copyright 2016 Daniel Kopko */
/*
 * This file is part of CB.
 *
 * CB is free software: you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * CB is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with CB.  If not, see <http://www.gnu.org/licenses/>.
 */
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
