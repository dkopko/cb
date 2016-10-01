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
#ifndef _CB_BST_H_
#define _CB_BST_H_

#include "cb.h"
#include "cb_hash.h"
#include "cb_term.h"


/* Immediate version */

/*
 * There is no equivalent of "NULL" for cb_offset_t, so we use an invalid
 * value as the sentinel.  The reason that the value of 1 is invalid is because
 * the offsets of cb_bst_node structs must have an alignment greater than
 * char-alignment, but 1 is not aligned to anything greater than char
 * alignment.  Declared as an enum for use as a constant.
 */
enum
{
    CB_BST_SENTINEL = 1
};

int
cb_bst_insert(struct cb            **cb,
              cb_offset_t           *header_offset,
              cb_offset_t            cutoff_offset,
              const struct cb_term  *key,
              const struct cb_term  *value);

int
cb_bst_lookup(const struct cb      *cb,
              cb_offset_t           header_offset,
              const struct cb_term *key,
              struct cb_term       *value);

int
cb_bst_delete(struct cb            **cb,
              cb_offset_t           *header_offset,
              cb_offset_t            cutoff_offset,
              const struct cb_term  *key);

bool
cb_bst_contains_key(const struct cb      *cb,
                    cb_offset_t           header_offset,
                    const struct cb_term *key);

typedef int (*cb_bst_traverse_func_t)(const struct cb_term *key,
                                      const struct cb_term *value,
                                      void                 *closure);

int
cb_bst_traverse(const struct cb        *cb,
                cb_offset_t             header_offset,
                cb_bst_traverse_func_t  func,
                void                   *closure);

void
cb_bst_print(struct cb   **cb,
             cb_offset_t   header_offset);

int
cb_bst_cmp(const struct cb *cb,
           cb_offset_t      lhs_header_offset,
           cb_offset_t      rhs_header_offset);

size_t
cb_bst_size(const struct cb *cb,
            cb_offset_t      header_offset);

void
cb_bst_hash_continue(cb_hash_state_t *hash_state,
                     const struct cb *cb,
                     cb_offset_t      header_offset);

cb_hash_t
cb_bst_hash(const struct cb *cb,
            cb_offset_t      header_offset);

int
cb_bst_render(cb_offset_t   *dest_offset,
              struct cb    **cb,
              cb_offset_t    header_offset,
              unsigned int   flags);

const char*
cb_bst_to_str(struct cb   **cb,
              cb_offset_t   header_offset);


/*
 * An iterator for BSTs.  This is a "fat"-style iterator because due to the
 * persistent nature of the BST data structure it is not possible to use parent
 * pointers.
 */
struct cb_bst_iter
{
    uint8_t      count;
    cb_offset_t  path_node_offset[64];
};


void
cb_bst_get_iter_start(const struct cb    *cb,
                      cb_offset_t         header_offset,
                      struct cb_bst_iter *iter);

void
cb_bst_get_iter_end(const struct cb    *cb,
                    cb_offset_t         header_offset,
                    struct cb_bst_iter *iter);

bool
cb_bst_iter_eq(struct cb_bst_iter *lhs,
               struct cb_bst_iter *rhs);

void
cb_bst_iter_next(const struct cb    *cb,
                 struct cb_bst_iter *iter);

void
cb_bst_iter_deref(const struct cb          *cb,
                  const struct cb_bst_iter *iter,
                  struct cb_term           *key,
                  struct cb_term           *value);

#endif /* ! defined _CB_BST_H_*/
