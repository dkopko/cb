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
#include "cb_hash.h"

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


union cb_raw_term
{
    uint64_t    u64;
    double      dbl;
    cb_offset_t bst;
    cb_offset_t structmap;
};


struct cb_term
{
    enum cb_term_tag  tag;
    union cb_raw_term value;
};


int
cb_term_cmp(const struct cb      *cb,
            const struct cb_term *lhs,
            const struct cb_term *rhs);

/*
 * Returns the size of a term's linked data (in the case of BSTs, for example).
 * Primitive terms such as u64s will return 0, as they have no external data.
 */
size_t
cb_term_external_size(const struct cb      *cb,
                      const struct cb_term *term);

/*
 * Accumulates the value of a term into an ongoing hashing operation.
 */
void
cb_term_hash_continue(cb_hash_state_t      *hash_state,
                      const struct cb      *cb,
                      const struct cb_term *term);

/*
 * Returns a hash value for the value of the term.
 */
cb_hash_t
cb_term_hash(const struct cb      *cb,
             const struct cb_term *term);

int
cb_term_render(cb_offset_t           *dest_offset,
               struct cb            **cb,
               const struct cb_term  *term,
               unsigned int           flags);

const char*
cb_term_to_str(struct cb            **cb,
               const struct cb_term  *term);


/*
 * Assigns the term 'rhs' to the term 'lhs'.  This variant is suitable for cases
 * where 'lhs' and 'rhs' are not known to be distinct.
 */
CB_INLINE void
cb_term_assign(struct cb_term *lhs, const struct cb_term *rhs)
{
    /*
     * Terms have value semantics, even when they represent a graph structure
     * (BSTs, for example).  This is ensured by the persistent nature
     * of such graph structures.  Because of this, a simple memmove will
     * suffice.
     */
    memmove(lhs, rhs, sizeof(*rhs));
}


/*
 * Assigns the term 'rhs' to the term 'lhs'.  This variant is suitable for cases
 * where 'lhs' and 'rhs' are known to be distinct.
 */
CB_INLINE void
cb_term_assign_restrict(struct cb_term *__restrict__       lhs,
                        const struct cb_term *__restrict__ rhs)
{
    /*
     * Terms have value semantics, even when they represent a graph structure
     * (BSTs, for example).  This is ensured by the persistent nature
     * of such graph structures.  Because of this, a simple memcpy() will
     * suffice.
     */
    memcpy(lhs, rhs, sizeof(*rhs));
}


/*
 * Returns the overall size of the term and its linked data (in the case of
 * BSTs, for example), sufficient for allocating a region of memory in a
 * continuous buffer into which the term's data can be consolidated.
 */
CB_INLINE size_t
cb_term_size(const struct cb      *cb,
             const struct cb_term *term)
{
    return sizeof(struct cb_term) + cb_term_external_size(cb, term);
}


CB_INLINE void
cb_term_set_u64(struct cb_term *term, uint64_t val)
{
    term->tag       = CB_TERM_U64;
    term->value.u64 = val;
}


CB_INLINE uint64_t
cb_term_get_u64(struct cb_term *term)
{
    cb_assert(term->tag == CB_TERM_U64);
    return term->value.u64;
}


CB_INLINE void
cb_term_set_dbl(struct cb_term *term, double val)
{
    term->tag       = CB_TERM_DBL;
    term->value.dbl = val;
}


CB_INLINE double
cb_term_get_dbl(struct cb_term *term)
{
    cb_assert(term->tag == CB_TERM_DBL);
    return term->value.dbl;
}


CB_INLINE void
cb_term_set_bst(struct cb_term *term, cb_offset_t bst_root)
{
    term->tag       = CB_TERM_BST;
    term->value.bst = bst_root;
}


CB_INLINE cb_offset_t
cb_term_get_bst(struct cb_term *term)
{
    cb_assert(term->tag == CB_TERM_BST);
    return term->value.bst;
}


CB_INLINE void
cb_term_set_structmap(struct cb_term *term, cb_offset_t structmap_root)
{
    term->tag             = CB_TERM_STRUCTMAP;
    term->value.structmap = structmap_root;
}


CB_INLINE cb_offset_t
cb_term_get_structmap(struct cb_term *term)
{
    cb_assert(term->tag == CB_TERM_STRUCTMAP);
    return term->value.structmap;
}


#endif /* ! defined _CB_TERM_H_*/
