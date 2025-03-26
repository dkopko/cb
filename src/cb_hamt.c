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

/* 
 * !!!!! NOTE! 2025-03-26 NOT YET FULLY REVIEWED OR TESTED !!!!!
 * cb_hamt is a product of the following prompt to CLine (claude-3.5-sonnet):
 * "Create a new datastructure ih the CB project: cb_hamt.  It should provide an interface similar to cb_bst (omittting any comparator arguments), but its internal implementation should be like that of cb_structmap_amt.  Presume insertions do not have the potential to cause collisions, and avoid any implementation of collision resolution."
 */

#include "cb_hamt.h"
#include "cb.h"
#include "cb_assert.h"
#include "cb_hash.h"
#include "cb_region.h"
#include "cb_term.h"
#include <stdio.h>
#include <string.h>

enum
{
    CB_HAMT_TYPE_NODE  = 0,
    CB_HAMT_TYPE_EMPTY = 1,
    CB_HAMT_TYPE_ITEM  = 2
};

static inline unsigned int
cb_hamt_hash_level(cb_hash_t hash, unsigned int level)
{
    return (hash >> (level * CB_HAMT_BITS)) & CB_HAMT_MASK;
}

int
cb_hamt_init(struct cb       **cb,
             struct cb_region *region,
             cb_offset_t     *new_header_offset_out)
{
    cb_offset_t header_offset;
    struct cb_hamt_header *header;
    int ret;

    ret = cb_region_memalign(cb, region, &header_offset,
                            cb_alignof(struct cb_hamt_header),
                            sizeof(struct cb_hamt_header));
    if (ret != CB_SUCCESS)
        return ret;

    header = cb_hamt_header_at(*cb, header_offset);
    header->total_internal_size = sizeof(struct cb_hamt_header);
    header->total_external_size = 0;
    header->num_entries = 0;
    header->hash_value = 0;
    header->root_node_offset = CB_HAMT_SENTINEL;

    *new_header_offset_out = header_offset;
    return CB_SUCCESS;
}

static int
cb_hamt_node_alloc(struct cb        **cb,
                   struct cb_region  *region,
                   struct cb_hamt_header *header,
                   cb_offset_t       *node_offset_out)
{
    cb_offset_t node_offset;
    struct cb_hamt_node *node;
    int ret;

    ret = cb_region_memalign(cb, region, &node_offset,
                            cb_alignof(struct cb_hamt_node),
                            sizeof(struct cb_hamt_node));
    if (ret != CB_SUCCESS)
        return ret;

    node = cb_hamt_node_at(*cb, node_offset);
    node->type = CB_HAMT_TYPE_EMPTY;
    memset(node->children, 0, sizeof(node->children));
    for (int i = 0; i < CB_HAMT_FANOUT; i++) {
        node->children[i] = CB_HAMT_SENTINEL;
    }

    header->total_internal_size += sizeof(struct cb_hamt_node);
    *node_offset_out = node_offset;
    return CB_SUCCESS;
}

int
cb_hamt_insert(struct cb           **cb,
               struct cb_region     *region,
               cb_offset_t         *header_offset,
               cb_offset_t          cutoff_offset,
               const struct cb_term *key,
               const struct cb_term *value)
{
    struct cb_hamt_header *header = cb_hamt_header_at(*cb, *header_offset);
    cb_offset_t *curr_offset = &header->root_node_offset;
    cb_hash_t hash = cb_term_hash(*cb, key);
    unsigned int level = 0;
    int ret;

    while (true) {
        if (*curr_offset == CB_HAMT_SENTINEL) {
            cb_offset_t new_node_offset;
            struct cb_hamt_node *new_node;

            ret = cb_hamt_node_alloc(cb, region, header, &new_node_offset);
            if (ret != CB_SUCCESS)
                return ret;

            new_node = cb_hamt_node_at(*cb, new_node_offset);
            new_node->type = CB_HAMT_TYPE_ITEM;
            new_node->hash_value = hash;
            cb_term_assign_restrict(&new_node->key, key);
            cb_term_assign_restrict(&new_node->value, value);

            *curr_offset = new_node_offset;
            header->num_entries++;
            header->total_external_size += cb_term_external_size(*cb, key) +
                                         cb_term_external_size(*cb, value);
            break;
        }

        struct cb_hamt_node *node = cb_hamt_node_at(*cb, *curr_offset);
        unsigned int index = cb_hamt_hash_level(hash, level);

        if (node->type == CB_HAMT_TYPE_EMPTY) {
            node->type = CB_HAMT_TYPE_ITEM;
            node->hash_value = hash;
            cb_term_assign_restrict(&node->key, key);
            cb_term_assign_restrict(&node->value, value);
            header->num_entries++;
            header->total_external_size += cb_term_external_size(*cb, key) +
                                         cb_term_external_size(*cb, value);
            break;
        }

        if (node->type == CB_HAMT_TYPE_ITEM) {
            if (cb_term_eq(*cb, &node->key, key)) {
                header->total_external_size -= cb_term_external_size(*cb, &node->value);
                cb_term_assign_restrict(&node->value, value);
                header->total_external_size += cb_term_external_size(*cb, value);
                break;
            }

            cb_offset_t new_node_offset;
            struct cb_hamt_node *new_node;
            
            ret = cb_hamt_node_alloc(cb, region, header, &new_node_offset);
            if (ret != CB_SUCCESS)
                return ret;

            new_node = cb_hamt_node_at(*cb, new_node_offset);
            new_node->type = CB_HAMT_TYPE_NODE;

            unsigned int old_index = cb_hamt_hash_level(node->hash_value, level);
            new_node->children[old_index] = *curr_offset;
            new_node->children[index] = CB_HAMT_SENTINEL;

            *curr_offset = new_node_offset;
            curr_offset = &new_node->children[index];
            level++;
            continue;
        }

        curr_offset = &node->children[index];
        level++;
    }

    return CB_SUCCESS;
}

int
cb_hamt_delete(struct cb           **cb,
               struct cb_region     *region,
               cb_offset_t         *header_offset,
               cb_offset_t          cutoff_offset,
               const struct cb_term *key)
{
    struct cb_hamt_header *header = cb_hamt_header_at(*cb, *header_offset);
    cb_offset_t *curr_offset = &header->root_node_offset;
    cb_hash_t hash = cb_term_hash(*cb, key);
    unsigned int level = 0;

    while (*curr_offset != CB_HAMT_SENTINEL) {
        struct cb_hamt_node *node = cb_hamt_node_at(*cb, *curr_offset);
        unsigned int index = cb_hamt_hash_level(hash, level);

        if (node->type == CB_HAMT_TYPE_ITEM) {
            if (cb_term_eq(*cb, &node->key, key)) {
                header->total_external_size -= cb_term_external_size(*cb, &node->key) +
                                             cb_term_external_size(*cb, &node->value);
                header->num_entries--;
                node->type = CB_HAMT_TYPE_EMPTY;
                return CB_SUCCESS;
            }
            return -1;
        }

        if (node->type == CB_HAMT_TYPE_EMPTY) {
            return -1;
        }

        curr_offset = &node->children[index];
        level++;
    }

    return -1;
}

int
cb_hamt_lookup(const struct cb    *cb,
               cb_offset_t         header_offset,
               const struct cb_term *key,
               struct cb_term      *value)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    cb_offset_t curr_offset = header->root_node_offset;
    cb_hash_t hash = cb_term_hash(cb, key);
    unsigned int level = 0;

    while (curr_offset != CB_HAMT_SENTINEL) {
        const struct cb_hamt_node *node = cb_hamt_node_at(cb, curr_offset);

        if (node->type == CB_HAMT_TYPE_ITEM) {
            if (cb_term_eq(cb, &node->key, key)) {
                cb_term_assign_restrict(value, &node->value);
                return CB_SUCCESS;
            }
            return -1;
        }

        if (node->type == CB_HAMT_TYPE_EMPTY) {
            return -1;
        }

        unsigned int index = cb_hamt_hash_level(hash, level);
        curr_offset = node->children[index];
        level++;
    }

    return -1;
}

bool
cb_hamt_contains_key(const struct cb    *cb,
                     cb_offset_t         header_offset,
                     const struct cb_term *key)
{
    struct cb_term value;
    return cb_hamt_lookup(cb, header_offset, key, &value) == CB_SUCCESS;
}

int
cb_hamt_traverse(const struct cb       *cb,
                 cb_offset_t            header_offset,
                 cb_hamt_traverse_func_t func,
                 void                   *closure)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    if (header->root_node_offset == CB_HAMT_SENTINEL)
        return CB_SUCCESS;

    struct {
        const struct cb_hamt_node *node;
        unsigned int index;
    } stack[64];  // Maximum tree height for 64-bit keys
    int stack_size = 0;

    stack[stack_size].node = cb_hamt_node_at(cb, header->root_node_offset);
    stack[stack_size].index = 0;
    stack_size++;

    while (stack_size > 0) {
        const struct cb_hamt_node *node = stack[stack_size - 1].node;
        unsigned int index = stack[stack_size - 1].index;

        if (index >= CB_HAMT_FANOUT) {
            stack_size--;
            continue;
        }

        stack[stack_size - 1].index++;

        if (node->type == CB_HAMT_TYPE_ITEM) {
            int ret = func(&node->key, &node->value, closure);
            if (ret != CB_SUCCESS)
                return ret;
            continue;
        }

        if (node->type == CB_HAMT_TYPE_NODE && node->children[index] != CB_HAMT_SENTINEL) {
            stack[stack_size].node = cb_hamt_node_at(cb, node->children[index]);
            stack[stack_size].index = 0;
            stack_size++;
        }
    }

    return CB_SUCCESS;
}

static int
cb_hamt_print_pair(const struct cb_term *key,
                   const struct cb_term *value,
                   void *closure)
{
    cb_term_print(key);
    printf(" -> ");
    cb_term_print(value);
    printf("\n");
    return CB_SUCCESS;
}

void
cb_hamt_print(struct cb   **cb,
              cb_offset_t   header_offset)
{
    cb_hamt_traverse(*cb, header_offset,
                    cb_hamt_print_pair, NULL);
}

size_t
cb_hamt_internal_size(const struct cb *cb,
                     cb_offset_t      header_offset)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    return header->total_internal_size;
}

size_t
cb_hamt_external_size(const struct cb *cb,
                     cb_offset_t      header_offset)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    return header->total_external_size;
}

int
cb_hamt_external_size_adjust(struct cb  *cb,
                           cb_offset_t  header_offset,
                           ssize_t      adjustment)
{
    struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    header->total_external_size = (size_t)((ssize_t)header->total_external_size + adjustment);
    return CB_SUCCESS;
}

size_t
cb_hamt_size(const struct cb *cb,
             cb_offset_t      header_offset)
{
    return cb_hamt_internal_size(cb, header_offset) +
           cb_hamt_external_size(cb, header_offset);
}

unsigned int
cb_hamt_num_entries(const struct cb *cb,
                    cb_offset_t     header_offset)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    return header->num_entries;
}

void
cb_hamt_hash_continue(cb_hash_state_t *hash_state,
                      const struct cb *cb,
                      cb_offset_t      header_offset)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    cb_hash_continue(hash_state, &header->hash_value, sizeof(header->hash_value));
}

cb_hash_t
cb_hamt_hash(const struct cb *cb,
             cb_offset_t      header_offset)
{
    const struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    return header->hash_value;
}

int
cb_hamt_render(cb_offset_t  *dest_offset,
               struct cb   **cb,
               cb_offset_t   header_offset,
               unsigned int  flags)
{
    *dest_offset = header_offset;
    return CB_SUCCESS;
}

const char*
cb_hamt_to_str(struct cb   **cb,
               cb_offset_t   header_offset)
{
    cb_hamt_print(cb, header_offset);
    return "";
}

int
cb_hamt_cmp(const struct cb *cb,
            cb_offset_t      lhs_header_offset,
            cb_offset_t      rhs_header_offset)
{
    const struct cb_hamt_header *lhs = cb_hamt_header_at(cb, lhs_header_offset);
    const struct cb_hamt_header *rhs = cb_hamt_header_at(cb, rhs_header_offset);

    if (!lhs && !rhs) return 0;
    if (!lhs) return -1;
    if (!rhs) return 1;

    if (lhs->num_entries < rhs->num_entries) return -1;
    if (lhs->num_entries > rhs->num_entries) return 1;

    if (lhs->hash_value < rhs->hash_value) return -1;
    if (lhs->hash_value > rhs->hash_value) return 1;

    return 0;
}
