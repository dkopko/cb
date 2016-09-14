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
cb_bst_insert(struct cb             **cb,
              cb_offset_t            *root_node_offset,
              cb_offset_t             cutoff_offset,
              const struct cb_key    *key,
              const struct cb_value  *value);

int
cb_bst_lookup(const struct cb     *cb,
              cb_offset_t          root_node_offset,
              const struct cb_key *key,
              struct cb_value     *value);

int
cb_bst_delete(struct cb             **cb,
              cb_offset_t            *root_node_offset,
              cb_offset_t             cutoff_offset,
              const struct cb_key    *key);

bool
cb_bst_contains_key(const struct cb     *cb,
                    cb_offset_t          root_node_offset,
                    const struct cb_key *key);

typedef int (*cb_bst_traverse_func_t)(const struct cb_key   *k,
                                      const struct cb_value *v,
                                      void                  *closure);

int
cb_bst_traverse(const struct cb        *cb,
                cb_offset_t             root_node_offset,
                cb_bst_traverse_func_t  func,
                void                   *closure);

void
cb_bst_print(const struct cb *cb,
             cb_offset_t      node_offset);

#endif /* ! defined _CB_BST_H_*/
