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
#ifndef _CB_HASH_H_
#define _CB_HASH_H_

#include "cb_assert.h"
#include "cb_misc.h"

#include <inttypes.h>
#include <stdint.h>

#define XXH_FORCE_NATIVE_FORMAT  1
#define XXH_PRIVATE_API          1
#include "../external/xxhash.h" // Adjusted path relative to src directory


typedef uint64_t      cb_hash_t;
typedef XXH64_state_t cb_hash_state_t;


CB_INLINE void
cb_hash_init(cb_hash_state_t *hash_state)
{
    XXH_errorcode err;

    (void)err;

    err = XXH64_reset((XXH64_state_t*)hash_state, 0);
    cb_assert(err == XXH_OK);
}


CB_INLINE void
cb_hash_continue(cb_hash_state_t *hash_state,
                 const void      *input,
                 size_t           length)
{
    XXH_errorcode err;

    (void)err;

    err = XXH64_update((XXH64_state_t*)hash_state, input, length);
    cb_assert(err == XXH_OK);
}


CB_INLINE cb_hash_t
cb_hash_finalize(const cb_hash_state_t *hash_state)
{
    return (cb_hash_t)XXH64_digest((const XXH64_state_t*)hash_state);
}

#endif /* ! defined _CB_HASH_H_*/
