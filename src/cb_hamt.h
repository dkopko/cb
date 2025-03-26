/* Copyright 2025 Daniel Kopko */
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
#ifndef _CB_HAMT_H_
#define _CB_HAMT_H_

/* 
 * !!!!! NOTE! 2025-03-26 NOT YET FULLY REVIEWED OR TESTED !!!!!
 * cb_hamt is a product of the following prompt to CLine (claude-3.5-sonnet):
 * "Create a new datastructure ih the CB project: cb_hamt.  It should provide an interface similar to cb_bst (omittting any comparator arguments), but its internal implementation should be like that of cb_structmap_amt.  Presume insertions do not have the potential to cause collisions, and avoid any implementation of collision resolution."
 */

#include "cb.h"
#include "cb_hash.h"
#include "cb_region.h"
#include "cb_term.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of bits per level */
#define CB_HAMT_BITS 6
#define CB_HAMT_FANOUT (1 << CB_HAMT_BITS)
#define CB_HAMT_MASK ((1 << CB_HAMT_BITS) - 1)

/* Immediate version */
enum
{
    CB_HAMT_SENTINEL = 1  /* Invalid alignment value as sentinel */
};

struct cb_hamt_header
{
    size_t        total_internal_size;
    size_t        total_external_size;
    unsigned int  num_entries;
    cb_hash_t     hash_value;
    cb_offset_t   root_node_offset;
};

struct cb_hamt_node
{
    struct cb_term key;
    struct cb_term value;
    unsigned int   type;    /* 0=node, 1=empty, 2=item */
    cb_hash_t      hash_value;
    cb_offset_t    children[CB_HAMT_FANOUT];
};

int
cb_hamt_init(struct cb       **cb,
             struct cb_region *region,
             cb_offset_t     *new_header_offset_out);

int
cb_hamt_insert(struct cb           **cb,
               struct cb_region     *region,
               cb_offset_t         *header_offset,
               cb_offset_t          cutoff_offset,
               const struct cb_term *key,
               const struct cb_term *value);

int
cb_hamt_lookup(const struct cb    *cb,
               cb_offset_t         header_offset,
               const struct cb_term *key,
               struct cb_term      *value);

int
cb_hamt_delete(struct cb           **cb,
               struct cb_region     *region,
               cb_offset_t         *header_offset,
               cb_offset_t          cutoff_offset,
               const struct cb_term *key);

bool
cb_hamt_contains_key(const struct cb    *cb,
                     cb_offset_t         header_offset,
                     const struct cb_term *key);

typedef int (*cb_hamt_traverse_func_t)(const struct cb_term *key,
                                     const struct cb_term *value,
                                     void                 *closure);

int
cb_hamt_traverse(const struct cb       *cb,
                 cb_offset_t            header_offset,
                 cb_hamt_traverse_func_t func,
                 void                   *closure);

void
cb_hamt_print(struct cb   **cb,
              cb_offset_t   header_offset);

int
cb_hamt_cmp(const struct cb *cb,
            cb_offset_t      lhs_header_offset,
            cb_offset_t      rhs_header_offset);

size_t
cb_hamt_internal_size(const struct cb *cb,
                     cb_offset_t      header_offset);

size_t
cb_hamt_external_size(const struct cb *cb,
                     cb_offset_t      header_offset);

int
cb_hamt_external_size_adjust(struct cb  *cb,
                           cb_offset_t  header_offset,
                           ssize_t      adjustment);

size_t
cb_hamt_size(const struct cb *cb,
             cb_offset_t      header_offset);

unsigned int
cb_hamt_num_entries(const struct cb *cb,
                    cb_offset_t     header_offset);

void
cb_hamt_hash_continue(cb_hash_state_t *hash_state,
                      const struct cb *cb,
                      cb_offset_t      header_offset);

cb_hash_t
cb_hamt_hash(const struct cb *cb,
             cb_offset_t      header_offset);

int
cb_hamt_render(cb_offset_t  *dest_offset,
               struct cb   **cb,
               cb_offset_t   header_offset,
               unsigned int  flags);

const char*
cb_hamt_to_str(struct cb   **cb,
               cb_offset_t   header_offset);

/* Helper functions */

CB_INLINE struct cb_hamt_header*
cb_hamt_header_at(const struct cb *cb,
                  cb_offset_t      header_offset)
{
    if (header_offset == CB_HAMT_SENTINEL)
        return NULL;

    return (struct cb_hamt_header*)cb_at(cb, header_offset);
}

CB_INLINE struct cb_hamt_node*
cb_hamt_node_at(const struct cb *cb,
                cb_offset_t      node_offset)
{
    if (node_offset == CB_HAMT_SENTINEL)
        return NULL;

    return (struct cb_hamt_node*)cb_at(cb, node_offset);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* ! defined _CB_HAMT_H_ */
