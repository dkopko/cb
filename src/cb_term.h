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
#ifndef _CB_TERM_H_
#define _CB_TERM_H_

#include "cb.h"

#include <inttypes.h>
#include <stdint.h>


enum cb_term_tag
{
    CB_TERM_U64,
    CB_TERM_DBL,
    CB_TERM_BST,
    CB_TERM_STRUCTMAP,
    /* FIXME, TODO:
     * CB_TERM_ATOM,
     * CB_TERM_TUPLE,
     * CB_TERM_STRUCT,
     * CB_TERM_STRING,
     * CB_TERM_*_ARR,
     * CB_TERM_HAMT
     */
    CB_TERM_TAG_MAX
};


struct cb_term
{
    unsigned int tag;
    union
    {
        uint64_t    u64;
        double      dbl;
        cb_offset_t bst;
        cb_offset_t structmap;
    } value;
};


CB_INLINE void
cb_term_set_u64(struct cb_term *term, uint64_t val)
{
    term->tag = CB_TERM_U64;
    term->value.u64 = val;
}


CB_INLINE uint64_t
cb_term_get_u64(struct cb_term *term)
{
    cb_assert(term->tag == CB_TERM_U64); /*FIXME will this suffice? */
    return term->value.u64;
}


CB_INLINE void
cb_term_assign(struct cb_term *lhs, const struct cb_term *rhs)
{
    /* FIXME do we need to handle anything with lower-bounds for aggregate
     * data structures here? */
    memmove(lhs, rhs, sizeof(*rhs));
}


int
cb_term_cmp(const struct cb      *cb,
            const struct cb_term *lhs,
            const struct cb_term *rhs);

int
cb_term_render(cb_offset_t           *dest_offset,
               struct cb            **cb,
               const struct cb_term  *term,
               unsigned int           flags);

const char*
cb_term_to_str(struct cb            **cb,
               const struct cb_term  *term);

#endif /* ! defined _CB_TERM_H_*/
