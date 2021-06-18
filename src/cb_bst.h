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
#include "cb_region.h"
#include "cb_term.h"

#ifdef __cplusplus
extern "C" {
#endif

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
cb_bst_init(struct cb               **cb,
            struct cb_region         *region,
            cb_offset_t              *new_header_offset_out,
            cb_term_comparator_t      key_term_cmp,
            cb_term_comparator_t      value_term_cmp,
            cb_term_render_t          key_term_render,
            cb_term_render_t          value_term_render,
            cb_term_external_size_t   key_term_external_size,
            cb_term_external_size_t   value_term_external_size);

int
cb_bst_insert(struct cb            **cb,
              struct cb_region      *region,
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
              struct cb_region      *region,
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
cb_bst_cmp(const struct cb      *cb,
           cb_offset_t           lhs_header_offset,
           cb_offset_t           rhs_header_offset);

size_t
cb_bst_internal_size_given_key_count(unsigned int keys);

size_t
cb_bst_internal_size(const struct cb *cb,
                     cb_offset_t      header_offset);

size_t
cb_bst_external_size(const struct cb *cb,
                     cb_offset_t      header_offset);

int
cb_bst_external_size_adjust(struct cb   *cb,
                            cb_offset_t  header_offset,
                            ssize_t      adjustment);

size_t
cb_bst_size(const struct cb *cb,
            cb_offset_t      header_offset);

unsigned int
cb_bst_num_entries(const struct cb *cb,
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
 * This header structure is used for O(1) maintenance and determinations of
 * cb_bst_size().  The 'total_internal_size' field represents the sum total of
 * the size of this header and the internal nodes. The 'total_external_size'
 * field represents the sum total of the size of the external structures rooted
 * at terms within keys or values of this BST.  The 'hash_value' field
 * represents a hash code for all the contained keys and values, but not the
 * internal structure of the BST.  This is because we want two BSTs to have the
 * same hash code if they contain the exact same set of key-value pairs,
 * regardless if their internal structure differs due to the order-of-operations
 * of the key-value pair insertions.
 */
struct cb_bst_header
{
    size_t                  total_internal_size;
    size_t                  total_external_size;
    unsigned int            num_entries;
    cb_hash_t               hash_value;
    cb_term_comparator_t    key_term_cmp;
    cb_term_comparator_t    value_term_cmp;
    cb_term_render_t        key_term_render;
    cb_term_render_t        value_term_render;
    cb_term_external_size_t key_term_external_size;
    cb_term_external_size_t value_term_external_size;
    cb_offset_t             root_node_offset;
};


struct cb_bst_node
{
    struct cb_term key;
    struct cb_term value;
    unsigned int   color;
    cb_hash_t      hash_value;
    cb_offset_t    child[2];  /* 0: left, 1: right */
};


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


CB_INLINE struct cb_bst_header*
cb_bst_header_at(const struct cb *cb,
                 cb_offset_t      header_offset)
{
    if (header_offset == CB_BST_SENTINEL)
        return NULL;

    return (struct cb_bst_header*)cb_at(cb, header_offset);
}

CB_INLINE cb_term_comparator_t
cb_bst_key_cmp_get(const struct cb *cb,
                   cb_offset_t      header_offset)
{
    struct cb_bst_header *header = cb_bst_header_at(cb, header_offset);

    if (!header)
      return &cb_term_cmp;

    return header->key_term_cmp;
}

CB_INLINE cb_term_render_t
cb_bst_key_render_get(const struct cb *cb,
                      cb_offset_t      header_offset)
{
    struct cb_bst_header *header = cb_bst_header_at(cb, header_offset);

    if (!header)
      return &cb_term_render;

    return header->key_term_render;
}

CB_INLINE cb_term_render_t
cb_bst_value_render_get(const struct cb *cb,
                        cb_offset_t      header_offset)
{
    struct cb_bst_header *header = cb_bst_header_at(cb, header_offset);

    if (!header)
      return &cb_term_render;

    return header->value_term_render;
}

CB_INLINE cb_term_external_size_t
cb_bst_key_term_external_size_get(const struct cb *cb,
                                  cb_offset_t      header_offset)
{
    struct cb_bst_header *header = cb_bst_header_at(cb, header_offset);

    if (!header)
      return NULL;

    return header->key_term_external_size;
}

CB_INLINE struct cb_bst_node*
cb_bst_node_at(const struct cb *cb,
               cb_offset_t      node_offset)
{
    if (node_offset == CB_BST_SENTINEL)
        return NULL;

    return (struct cb_bst_node*)cb_at(cb, node_offset);
}


CB_INLINE void
cb_bst_get_iter_end(const struct cb    *cb,
                    cb_offset_t         header_offset,
                    struct cb_bst_iter *iter)
{
    (void)cb, (void)header_offset;
    iter->count = 0;
}


CB_INLINE void
cb_bst_get_iter_start(const struct cb    *cb,
                      cb_offset_t         header_offset,
                      struct cb_bst_iter *iter)
{
    cb_offset_t curr_node_offset;

    if (header_offset == CB_BST_SENTINEL)
    {
        cb_bst_get_iter_end(cb, header_offset, iter);
        return;
    }

    curr_node_offset = cb_bst_header_at(cb, header_offset)->root_node_offset;

    iter->count = 0;
    while (curr_node_offset != CB_BST_SENTINEL)
    {
        iter->path_node_offset[iter->count] = curr_node_offset;
        curr_node_offset = cb_bst_node_at(cb, curr_node_offset)->child[0];
        iter->count++;
    }
}


CB_INLINE bool
cb_bst_iter_eq(struct cb_bst_iter *lhs,
               struct cb_bst_iter *rhs)
{
    if (lhs->count != rhs->count)
        return false;

    for (uint8_t i = 0; i < lhs->count; ++i)
        if (lhs->path_node_offset[i] != rhs->path_node_offset[i])
            return false;

    return true;
}


CB_INLINE void
cb_bst_iter_next(const struct cb    *cb,
                 struct cb_bst_iter *iter)
{
    cb_offset_t curr_node_offset;

    cb_assert(iter->count > 0);

    curr_node_offset =
        cb_bst_node_at(cb, iter->path_node_offset[iter->count - 1])->child[1];
    iter->count--;
    while (curr_node_offset != CB_BST_SENTINEL)
    {
        iter->path_node_offset[iter->count] = curr_node_offset;
        curr_node_offset = cb_bst_node_at(cb, curr_node_offset)->child[0];
        iter->count++;
    }
}


CB_INLINE void
cb_bst_iter_deref(const struct cb          *cb,
                  const struct cb_bst_iter *iter,
                  struct cb_term           *key,
                  struct cb_term           *value)
{
    struct cb_bst_node *curr_node;

    curr_node = cb_bst_node_at(cb, iter->path_node_offset[iter->count - 1]);
    cb_term_assign_restrict(key,   &(curr_node->key));
    cb_term_assign_restrict(value, &(curr_node->value));
}


CB_INLINE int
cb_bst_iter_visit(const struct cb          *cb,
                  const struct cb_bst_iter *iter,
                  cb_bst_traverse_func_t    func,
                  void                     *closure)
{
    struct cb_bst_node *curr_node;
    curr_node = cb_bst_node_at(cb, iter->path_node_offset[iter->count - 1]);
    return func(&(curr_node->key), &(curr_node->value), closure);
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* ! defined _CB_BST_H_*/
