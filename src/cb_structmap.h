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
#ifndef _CB_STRUCTMAP_H_
#define _CB_STRUCTMAP_H_

#include "cb.h"
#include "cb_hash.h"

/*
 * The purpose of the cb_structmap is to contain a mapping of cb_struct_id_t ->
 * cb_offset_t.  Ideally this would be O(1), but that would imply some kind of
 * hashtable and have undesirable repercussions in terms of mutability,
 * resizing, etc. Instead, we use the O(log16 n) structure of an Array Mapped
 * Trie (Bagwell), which is easily made into an efficient persistent data
 * structure that will dovetail nicely with other aspects of our system.
 */

typedef uint64_t cb_struct_id_t;

/*
 * There is no equivalent of "NULL" for cb_offset_t, so we use an invalid
 * value as the sentinel.  The reason that the value of 1 is invalid is because
 * the offsets of cb_structmap_node structs must have an alignment greater than
 * char-alignment, but 1 is not aligned to anything greater than char
 * alignment.  Declared as an enum for use as a constant.
 */
enum
{
    CB_STRUCTMAP_SENTINEL = 1
};

void
cb_structmap_print(const struct cb *cb,
                   cb_offset_t      node_offset);

int
cb_structmap_insert(struct cb      **cb,
                    cb_offset_t     *root_node_offset,
                    cb_offset_t      cutoff_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t      struct_offset);

int
cb_structmap_lookup(const struct cb *cb,
                    cb_offset_t      root_node_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t     *struct_offset);

int
cb_structmap_delete(struct cb      **cb,
                    cb_offset_t     *root_node_offset,
                    cb_offset_t      cutoff_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t     *struct_offset);

int
cb_structmap_cmp(const struct cb *cb,
                 cb_offset_t      lhs,
                 cb_offset_t      rhs);

size_t
cb_structmap_size(const struct cb *cb,
                  cb_offset_t      root_node_offset);

void
cb_structmap_hash_continue(cb_hash_state_t *hash_state,
                           const struct cb *cb,
                           cb_offset_t      root_node_offset);

cb_hash_t
cb_structmap_hash(const struct cb *cb,
                  cb_offset_t      root_node_offset);

int
cb_structmap_render(cb_offset_t   *dest_offset,
                    struct cb    **cb,
                    cb_offset_t    root_node_offset,
                    unsigned int   flags);

const char*
cb_structmap_to_str(struct cb   **cb,
                    cb_offset_t   root_node_offset);

#endif /* _CB_AMT_H_ */
