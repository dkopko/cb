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
#include "cb_term.h"

#include "cb_bst.h"
#include "cb_print.h"
#include "cb_structmap.h"

#include <stdio.h>


int
cb_term_cmp(const struct cb      *cb,
            const struct cb_term *lhs,
            const struct cb_term *rhs)
{
    /* Terms are primarily ordered by the tag (type). */
    if (lhs->tag < rhs->tag) return -1;
    if (lhs->tag > rhs->tag) return 1;

    /* Terms are secondarily ordered by their value. */
    switch (lhs->tag)
    {
        case CB_TERM_U64:
            if (lhs->value.u64 < rhs->value.u64) return -1;
            if (lhs->value.u64 > rhs->value.u64) return 1;
            return 0;

        case CB_TERM_DBL:
            if (lhs->value.dbl < rhs->value.dbl) return -1;
            if (lhs->value.dbl > rhs->value.dbl) return 1;
            return 0;

        case CB_TERM_BST:
            return cb_bst_cmp(cb, lhs->value.bst, rhs->value.bst);

        case CB_TERM_STRUCTMAP:
            return cb_structmap_cmp(cb,
                                    lhs->value.structmap,
                                    rhs->value.structmap);

        default:
            /* Unreachable */
            abort();
    }
}


size_t
cb_term_external_size(const struct cb      *cb,
                      const struct cb_term *term)
{
    /*
     * Terms types which have external structure need to include the size
     * of their external structure.
     */
    switch (term->tag)
    {
        case CB_TERM_BST:
            return cb_bst_size(cb, term->value.bst);

        case CB_TERM_STRUCTMAP:
            return cb_structmap_size(cb, term->value.structmap);

        default:
            return 0;
    }
}


cb_hash_t
cb_term_hash(const struct cb      *cb,
             const struct cb_term *term)
{
    cb_hash_state_t hash_state;

    cb_hash_init(&hash_state);
    cb_term_hash_continue(&hash_state, cb, term);
    return cb_hash_finalize(&hash_state);
}


int
cb_term_render(cb_offset_t           *dest_offset,
               struct cb            **cb,
               const struct cb_term  *term,
               unsigned int           flags)
{
    switch (term->tag)
    {
        case CB_TERM_U64:
            return cb_asprintf(dest_offset, cb, "%" PRIu64, term->value.u64);

        case CB_TERM_DBL:
            return cb_asprintf(dest_offset, cb, "%f" , term->value.dbl);

        case CB_TERM_BST:
            return cb_bst_render(dest_offset, cb, term->value.bst, flags);

        case CB_TERM_STRUCTMAP:
            return cb_structmap_render(dest_offset, cb, term->value.bst, flags);

        default:
            /*Unreachable*/
            abort();
    }
}


const char*
cb_term_to_str(struct cb            **cb,
               cb_term_render_t       render,
               const struct cb_term  *term)
{
    cb_offset_t dest_offset;
    int ret;

    ret = render(&dest_offset, cb, term, CB_RENDER_DEFAULT);
    if (ret != 0)
        return "(render-error)";

    return (const char*)cb_at(*cb, dest_offset);
}
