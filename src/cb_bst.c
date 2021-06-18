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
#include "cb.h"
#include "cb_assert.h"
#include "cb_bst.h"
#include "cb_hash.h"
#include "cb_print.h"
#include "cb_term.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/*

Red-Black trees
---------------
* NULL pointers cannot be used as there are no equivalents under cb_offset_t.
  Instead, a sentinel value or the equivalent of pointer-tagging must be used.
* It is possible to use something akin to pointer tagging for the red/black
  coloring.
* To construct a red-black tree, we can:
  1) Write the sentinel node.
  2) Traverse prev offsets from the latest command:
     2a) if the command is a KEYVAL, incorporate it into the red-black tree.
     2b) if the command is a RBTREE, merge the being-built tree with this
         already-existing tree and stop.  The merge operation can create new
         nodes needed to link the two subtrees into one and it can modify nodes
         in place within the being-built tree, but it must not modify any nodes
         in the already-existing tree.
     2c) if the command represents the consolidated post-GC data, stop.
  3) As "NULL" links need to be written, instead point them to the sentinel
     node (or else pointer-tag them appropriately).
* It is ok for the being-built tree to be mutated in place.  However, once this
  tree has been constructed and then "published", it should not be mutated.
* Q: How are deletions done in general?
* Q: How are deletions done when merging a being-built red-black tree with an
     already-built red-black tree.
* Q: Do we need an alternate O(log n) implementation to red-black trees?
     1) left-leaning red-black trees,
        A: NO!, see http://www.read.seas.harvard.edu/~kohler/notes/llrb.html
     2) 2-3 trees,
     3) 2-3-4 trees
* Q: If we want to pre-allocate space for a red-black tree (for GC or some other
     reason) what is the expression which yield the total space needed?
* Q: Do we need a parent pointer?
* Q: Do we need next/prev pointers ("threaded" trees), for constant time access
     of ordered elements?


Garbage Collection
------------------
* The garbage collector must reserve enough memory for:
  1) it's own working-space for any intermediate data structures.
  2) the final consolidation data structure (a k[],v[]; a red-black tree; or
     some other structure),
  Also, it may be the case that the GC is written to do an initial pass over the
  data (to determine an exact number of unique keys, for example, instead of
  only a maximum, which would allow a more precise collection), in which case
  there may be some max()-like considerations across all stages of the GC
  process, where some portions of memory of the final consolidation area are
  also used in earlier processing and then overwritten with final data in later
  stages.
* It would be best if the intermediate data structures were stored before the
  final data structure in the cb, as that would allow them to be disposed of
  more easily (being contiguous with lower, now-unnecessary entries in the cb)
  after the garbage collection is complete.
* The garbage collector's data area should start at least on a cache-aligned
  boundary, and possibly on a page-aligned boundary.  I suspect that
  page-aligned may be better for NUMA considerations in case the GC thread gets
  moved to another CPU (though not sure about this).
* There should be two communication areas in the cb:
  1) input parameters to the garbage collector, which may contain some of the
     following:
     a) the offset of the command at which GC is to begin (collecting data of
        all prior commands),
     b) a notification variable (futex?) which an independent GC thread can wait
        on,
     c) the maximum number of unique keys, (This would be useful to determine
        how large a key array, k[], needs to potentially be.  The reason this is
        mentioned as a maximum instead of an exact number is that appends of
        k=v assignments may have displaced earlier assignments to the same key,
        but we can't know unless there's been some consolidation.)
     d) The exact size (including necessary alignment padding) of all of the
        key data,
     e) The exact size (including necessary alignment padding) of all of the
        value data.
  2) response data from the garbage collector, which may contain some of the
     following:
     a) cb_offset_t cutoff_offset, equal to the input-parameter at which GC was
        to begin;
        This value signifies a point at which traversal of command prev fields
        stops and the GC's output command is referred to instead.  The GC's
        output command is something like a k[],v[] or the head of a red-black
        tree.
     b) A notification value signifying the GC is done;
        This value can be sampled by the mutator thread during any of its cb
        mutations or otherwise periodically.  If the notification has fired,
        then the mutator thread should incorporate the cutoff offset into its
        cb's state, such that it will be affect prev traversals.
        Also, once the notification has fired, the mutator can mark as free any
        data <= (cutoff_offset + sizeof(struct cb_command_any) +
        sizeof(GC working-space)).
     c) waste amount;
        This is the sum total of any padding between keys, between values,
        between the k[] array and the v[] array, as well as any added space
        reserved due to the estimated maximum number of unique keys being
        greater than the actual.

* If the garbage collector and the cb mutator are separate threads, then a
  memory barrier should be used before any cross-thread notification variable
  is written to in the communication areas.


*/


struct cb_bst_mutate_state
{
    cb_offset_t greatgrandparent_node_offset;
    cb_offset_t grandparent_node_offset;
    cb_offset_t parent_node_offset;
    cb_offset_t curr_node_offset;
    cb_offset_t sibling_node_offset;  /* Only maintained for deletes. */
    cb_offset_t new_header_offset;
    cb_offset_t new_root_node_offset;
    cb_offset_t cutoff_offset;
    int         greatgrandparent_to_grandparent_dir; /* Only maintained for inserts. */
    int         grandparent_to_parent_dir;
    int         parent_to_curr_dir;
    int         dir;
};


static const struct cb_bst_mutate_state CB_BST_MUTATE_STATE_INIT =
    {
        .greatgrandparent_node_offset        = CB_BST_SENTINEL,
        .grandparent_node_offset             = CB_BST_SENTINEL,
        .parent_node_offset                  = CB_BST_SENTINEL,
        .curr_node_offset                    = CB_BST_SENTINEL,
        .sibling_node_offset                 = CB_BST_SENTINEL,
        .new_root_node_offset                = CB_BST_SENTINEL,
        .cutoff_offset                       = CB_BST_SENTINEL,
        .greatgrandparent_to_grandparent_dir = 1,
        .grandparent_to_parent_dir           = 1,
        .parent_to_curr_dir                  = 1,
        .dir                                 = 1
    };


enum
{
    CB_BST_BLACK = 0,
    CB_BST_RED = 1
};


struct cb_bst_sequence_check_state
{
    struct cb      **cb;
    cb_offset_t      header_offset;
    bool             has_prev_key;
    unsigned int     i;
    struct cb_term   prev_key;
    bool             failed;
    bool             do_print;
};


CB_INLINE bool
cb_bst_node_is_modifiable(cb_offset_t node_offset,
                          cb_offset_t cutoff_offset)
{
//FIXME handle reversed regions
    int cmp = cb_offset_cmp(node_offset, cutoff_offset);
    cb_assert(cmp == -1 || cmp == 0 || cmp == 1);
    return cmp > -1;
}


CB_INLINE bool
cb_bst_node_is_red(const struct cb *cb,
                   cb_offset_t      node_offset)
{
    struct cb_bst_node *node = cb_bst_node_at(cb, node_offset);
    return node && node->color == CB_BST_RED;
}


CB_INLINE bool
cb_bst_node_is_black(const struct cb *cb,
                     cb_offset_t      node_offset)
{
    struct cb_bst_node *node = cb_bst_node_at(cb, node_offset);
    return !node || node->color == CB_BST_BLACK;
}


static cb_hash_t
cb_bst_node_hash(const struct cb          *cb,
                 const struct cb_bst_node *node)
{
    cb_hash_state_t hash_state;

    cb_hash_init(&hash_state);
    cb_term_hash_continue(&hash_state, cb, &(node->key));
    cb_term_hash_continue(&hash_state, cb, &(node->value));

    return cb_hash_finalize(&hash_state);
}


static int
cb_bst_sequence_check(const struct cb_term *key,
                      const struct cb_term *value,
                      void                 *closure)
{
    struct cb_bst_sequence_check_state *scs =
        (struct cb_bst_sequence_check_state *)closure;
    struct cb_bst_header *header;

    header = cb_bst_header_at(*(scs->cb), scs->header_offset);

    (void)value;

    if (scs->do_print)
        cb_log_debug("bst[%u] = %s.", scs->i, cb_term_to_str(scs->cb, header->key_term_render, key));

    if (scs->has_prev_key &&
        header->key_term_cmp(*(scs->cb), &(scs->prev_key), key) != -1)
    {
        if (scs->do_print)
        {
            cb_log_debug("Order violation: %s !< %s",
                         cb_term_to_str(scs->cb, header->key_term_render, &(scs->prev_key)),
                         cb_term_to_str(scs->cb, header->key_term_render, key));
        }

        scs->failed = true;
    }

    scs->has_prev_key = true;
    (scs->i)++;
    cb_term_assign(&(scs->prev_key), key);
    return 0;
}


static bool
cb_bst_validate_sequence(struct cb   **cb,
                         cb_offset_t   header_offset,
                         bool          do_print)
{
    struct cb_bst_sequence_check_state scs = {
        .cb            = cb,
        .header_offset = header_offset,
        .has_prev_key  = false,
        .i             = 0,
        .failed        = false,
        .do_print      = do_print
    };
    int ret;

    (void)ret;

    ret = cb_bst_traverse(*cb, header_offset, cb_bst_sequence_check, &scs);
    cb_assert(ret == 0);

    return scs.failed ? false : true;
}


static bool
cb_bst_validate_structure(struct cb                        **cb,
                          cb_offset_t                        node_offset,
                          cb_term_comparator_t               key_term_cmp,
                          cb_term_render_t                   key_term_render,
                          cb_term_render_t                   value_term_render,
                          uint32_t                          *tree_height,
                          uint32_t                           validate_depth,
                          bool                               do_print,
                          const struct cb_bst_mutate_state  *s)
{
    struct cb_bst_node *node, *left_node, *right_node;
    uint32_t left_height = 0, right_height = 0;
    bool retval = true;
    static char spaces[] = "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t";

    if (node_offset == CB_BST_SENTINEL)
    {
        *tree_height = 0;
        return true;
    }

    node = cb_bst_node_at(*cb, node_offset);
    if (do_print)
        printf("%.*s%snode_offset %ju: {k: %s, v: %s, color: %s, left: %ju, right: %ju}%s%s%s%s%s\n",
               (int)validate_depth, spaces,
               node->color == CB_BST_RED ? "\033[1;31;40m" : "",
               node_offset,
               cb_term_to_str(cb, key_term_render, &(node->key)),
               cb_term_to_str(cb, value_term_render, &(node->value)),
               node->color == CB_BST_RED ? "RED" : "BLACK",
               (uintmax_t)node->child[0],
               (uintmax_t)node->child[1],
               node->color == CB_BST_RED ? "\033[0m" : "",
               (s && node_offset == s->greatgrandparent_node_offset ? " GREATGRANDPARENT" : ""),
               (s && node_offset == s->grandparent_node_offset ? " GRANDPARENT" : ""),
               (s && node_offset == s->parent_node_offset ? " PARENT" : ""),
               (s && node_offset == s->curr_node_offset ? " CURRENT" : ""));

    left_node = cb_bst_node_at(*cb, node->child[0]);
    if (left_node)
    {
        if (key_term_cmp(*cb, &(left_node->key), &(node->key)) != -1)
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju: left key %s (off: %ju) !< key %s\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset,
                       cb_term_to_str(cb, key_term_render, &(left_node->key)),
                       node->child[0],
                       cb_term_to_str(cb, key_term_render, &(node->key)));
            retval = false;
        }
    }

    right_node = cb_bst_node_at(*cb, node->child[1]);
    if (right_node)
    {
        if (key_term_cmp(*cb, &(node->key), &(right_node->key)) != -1)
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju: key %s !< right key %s (off:%ju)\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset,
                       cb_term_to_str(cb, key_term_render, &(node->key)),
                       cb_term_to_str(cb, key_term_render, &(right_node->key)),
                       node->child[1]);
            retval = false;
        }
    }

    /* Validate left subtree. */
    if (!cb_bst_validate_structure(cb,
                                   node->child[0],
                                   key_term_cmp,
                                   key_term_render,
                                   value_term_render,
                                   &left_height,
                                   validate_depth + 1,
                                   do_print,
                                   s))
    {
        retval = false;
    }

    /* Validate right subtree. */
    if (!cb_bst_validate_structure(cb,
                                  node->child[1],
                                  key_term_cmp,
                                  key_term_render,
                                  value_term_render,
                                  &right_height,
                                  validate_depth + 1,
                                  do_print,
                                  s))
    {
        retval = false;
    }

    if (left_height != right_height)
    {
        if (do_print)
            printf("%*.s\033[1;33;40mnode_offset %ju: left height %" PRIu32 " != right height %" PRIu32 "\033[0m\n",
                   (int)validate_depth, spaces,
                   node_offset, left_height, right_height);
        retval = false;
    }

    if (node->color == CB_BST_RED)
    {
        if (cb_bst_node_is_red(*cb, node->child[0]))
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju (red) has red left child %ju\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset, node->child[0]);
            retval = false;
        }

        if (cb_bst_node_is_red(*cb, node->child[1]))
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju (red) has red right child %ju\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset, node->child[1]);
            retval = false;
        }
    }

    *tree_height = (node->color == CB_BST_BLACK ? 1 : 0)
                   + (left_height < right_height ? right_height : left_height);
    return retval;
}

struct sum_external_size_closure
{
    struct cb               **cb;
    size_t                    keys_total_external_size;
    size_t                    values_total_external_size;
    cb_term_render_t          key_term_render;
    cb_term_render_t          value_term_render;
    cb_term_external_size_t   key_term_external_size;
    cb_term_external_size_t   value_term_external_size;
};

static int
sum_external_size(const struct cb_term *key,
                  const struct cb_term *value,
                  void                 *closure)
{
    struct sum_external_size_closure *sesc = (struct sum_external_size_closure *)closure;

    sesc->keys_total_external_size += sesc->key_term_external_size(*(sesc->cb), key);
    sesc->values_total_external_size += sesc->value_term_external_size(*(sesc->cb), value);

    return 0;
}

static bool
cb_bst_validate_external_size(struct cb   **cb,
                              cb_offset_t   header_offset,
                              bool          do_print)
{
    struct cb_bst_header *header = cb_bst_header_at(*cb, header_offset);
    struct sum_external_size_closure sesc = {
        .cb = cb,
        .keys_total_external_size = 0,
        .values_total_external_size = 0,
        .key_term_render = header->key_term_render,
        .value_term_render = header->value_term_render,
        .key_term_external_size = header->key_term_external_size,
        .value_term_external_size = header->value_term_external_size
    };
    int ret;

    (void)ret;

    ret = cb_bst_traverse(*cb, header_offset, sum_external_size, &sesc);
    cb_assert(ret == 0);

    size_t actual_bst_external_size = sesc.keys_total_external_size + sesc.values_total_external_size;
    size_t calculated_bst_external_size = cb_bst_external_size(*cb, header_offset);
    if (do_print) {
        printf("actual_bst_external_size:%zu, calculated_bst_external_size:%zu\n",
               actual_bst_external_size, calculated_bst_external_size);
    }

    return (actual_bst_external_size <= calculated_bst_external_size);
}

static bool
cb_bst_validate(struct cb                       **cb,
                cb_offset_t                       header_offset,
                const char                       *name,
                const struct cb_bst_mutate_state *s)
{
    struct cb_bst_header *header;
    cb_offset_t           root_node_offset;
    uint32_t              tree_height;
    bool                  sequence_ok,
                          structure_ok,
                          external_size_ok;

    (void)s;

    if (header_offset == CB_BST_SENTINEL)
        return true;

    header = cb_bst_header_at(*cb, header_offset);
    root_node_offset = header->root_node_offset;

    /* First, just validate without printing. */
    sequence_ok  = cb_bst_validate_sequence(cb, header_offset, false);
    structure_ok = cb_bst_validate_structure(cb,
                                             root_node_offset,
                                             header->key_term_cmp,
                                             header->key_term_render,
                                             header->value_term_render,
                                             &tree_height,
                                             0,
                                             false,
                                             s);
    external_size_ok = cb_bst_validate_external_size(cb, header_offset, false);
    if (sequence_ok && structure_ok && external_size_ok)
        return true;

    /* If that failed, go through again with printing of problems. */
    if (!sequence_ok)
    {
        cb_log_error("BEGIN ERROR PRINT OF SEQUENCE %s",
                     name == NULL ? "" : name);

        cb_bst_validate_sequence(cb, header_offset, true);

        cb_log_error("END   ERROR PRINT OF SEQUENCE %s",
                     name == NULL ? "" : name);
    }

    if (!structure_ok)
    {
        cb_log_error("BEGIN ERROR PRINT OF STRUCTURE %s",
                     name == NULL ? "" : name);

        cb_bst_validate_structure(cb,
                                  root_node_offset,
                                  header->key_term_cmp,
                                  header->key_term_render,
                                  header->value_term_render,
                                  &tree_height,
                                  0,
                                  true,
                                  s);

        cb_log_error("END   ERROR PRINT OF STRUCTURE %s",
                     name == NULL ? "" : name);
    }

    if (!external_size_ok) {
        cb_log_error("Bad external size %s", name == NULL ? "" : name);
        cb_bst_validate_external_size(cb, header_offset, true);
    }

    return false;
}


static bool
cb_bst_mutate_state_validate(struct cb                  *cb,
                             struct cb_bst_mutate_state *s)
{
    bool is_ok = true;

    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] != s->grandparent_node_offset)
        {
            is_ok = false;
            cb_log_error("greatgrandparent doesn't point to grandparent");
        }
    }

    if (s->grandparent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->grandparent_node_offset)->child[
               s->grandparent_to_parent_dir] != s->parent_node_offset)
        {
            is_ok = false;
            cb_log_error("grandparent doesn't point to parent");
        }
    }

    if (s->parent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->parent_node_offset)->child[
               s->parent_to_curr_dir] != s->curr_node_offset)
        {
            is_ok = false;
            cb_log_error("parent doesn't point to current");
        }
    }

    if (s->sibling_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->parent_node_offset)->child[
               !s->parent_to_curr_dir] != s->sibling_node_offset)
        {
            is_ok = false;
            cb_log_error("parent doesn't point to sibling");
        }
    }

    if (!is_ok)
    {
        cb_log_error("greatgrandparent_node_offset: %ju",
                     (uintmax_t)s->greatgrandparent_node_offset);
        cb_log_error("grandparent_node_offset: %ju",
                     (uintmax_t)s->grandparent_node_offset);
        cb_log_error("parent_node_offset: %ju",
                     (uintmax_t)s->parent_node_offset);
        cb_log_error("curr_node_offset: %ju",
                     (uintmax_t)s->curr_node_offset);
        cb_log_error("sibling_node_offset: %ju",
                     (uintmax_t)s->sibling_node_offset);
        cb_log_error("new_root_node_offset: %ju",
                     (uintmax_t)s->new_root_node_offset);
        cb_log_error("greatgrandparent_to_grandparent_dir: %d",
                     s->greatgrandparent_to_grandparent_dir);
        cb_log_error("grandparent_to_parent_dir: %d",
                     s->grandparent_to_parent_dir);
        cb_log_error("parent_to_curr_dir: %d",
                     s->parent_to_curr_dir);
        cb_log_error("dir: %d",
                     s->dir);
    }

    return is_ok;
}


static int
cb_bst_header_alloc(struct cb        **cb,
                    struct cb_region  *region,
                    cb_offset_t       *header_offset)
{
    cb_offset_t new_header_offset;
    int ret;

    ret = cb_region_memalign(cb,
                             region,
                             &new_header_offset,
                             cb_alignof(struct cb_bst_header),
                             sizeof(struct cb_bst_header));
    if (ret != CB_SUCCESS)
        return ret;

    *header_offset = new_header_offset;

    return 0;
}


static int
cb_bst_node_alloc(struct cb        **cb,
                  struct cb_region  *region,
                  cb_offset_t       *node_offset)
{
    cb_offset_t new_node_offset;
    int ret;

    ret = cb_region_memalign(cb,
                             region,
                             &new_node_offset,
                             cb_alignof(struct cb_bst_node),
                             sizeof(struct cb_bst_node));
    if (ret != CB_SUCCESS)
        return ret;

    *node_offset = new_node_offset;

    return 0;
}


/*
 *  Returns 0 if found or -1 if not found.
 *  If found, iter->path_node_offset[iter->count - 1] will point to the node
 *     containing key.
 *  If not found, iter->path_node_offset[iter->count - 1] will point to the
 *     parent node for which a node containing key may be inserted.
 */
static int
cb_bst_find_path(const struct cb      *cb,
                 cb_offset_t           header_offset,
                 struct cb_bst_iter   *iter,
                 const struct cb_term *key)
{
    struct cb_bst_header *header;
    cb_offset_t           curr_offset;
    struct cb_bst_node   *curr_node;
    int                   cmp;

    header = cb_bst_header_at(cb, header_offset);

    iter->count = 0;

    curr_offset = header->root_node_offset;

    while ((curr_node = cb_bst_node_at(cb, curr_offset)) != NULL)
    {
        iter->path_node_offset[iter->count] = curr_offset;
        iter->count++;

        //FIXME the following is dangerous (fakes **cb), and should only be used for debugging.
        //printf("About to compare %s with %s\n",
        //       cb_term_to_str((struct cb**)&cb, cb_bst_render_get(cb, header_offset), key),
        //       cb_term_to_str((struct cb**)&cb, cb_bst_render_get(cb, header_offset), &(curr_node->key)));
        cmp = header->key_term_cmp(cb, key, &(curr_node->key));
        if (cmp == 0)
            return 0; /* FOUND */

        cb_assert(cmp == -1 || cmp == 1);
        curr_offset = (cmp == -1 ? curr_node->child[0] : curr_node->child[1]);
    }

    return -1; /* NOT FOUND */
}


bool
cb_bst_contains_key(const struct cb      *cb,
                    cb_offset_t           header_offset,
                    const struct cb_term *key)
{
    struct cb_bst_iter    iter;
    int ret;

    if (header_offset == CB_BST_SENTINEL)
        return false;

    ret = cb_bst_find_path(cb,
                           header_offset,
                           &iter,
                           key);
    return (ret == 0);
}


int
cb_bst_lookup(const struct cb      *cb,
              cb_offset_t           header_offset,
              const struct cb_term *key,
              struct cb_term       *value)
{
    struct cb_bst_iter    iter;
    struct cb_term        ignored_key;
    int ret;

    if (header_offset == CB_BST_SENTINEL)
    {
        ret = -1;
        goto fail;
    }

    /*
     * NOTE: cb would only need mutation on structural failures (for printing of
     * the errors), so we don't pollute the signature of cb_bst_lookup() here,
     * but rather hack it with a cast.
     */
    cb_heavy_assert(cb_bst_validate((struct cb **)&cb,
                                    header_offset,
                                    "pre-lookup",
                                    NULL));

    ret = cb_bst_find_path(cb,
                           header_offset,
                           &iter,
                           key);
    if (ret != 0)
        goto fail;

    cb_bst_iter_deref(cb, &iter, &ignored_key, value);

fail:
    /* See NOTE above. */
    cb_heavy_assert(cb_bst_validate((struct cb **)&cb,
                                    header_offset,
                                    (ret == 0 ? "post-lookup-success" :
                                                "post-lookup-fail"),
                                    NULL));
    return ret;
}


static int
cb_bst_select_modifiable_header(struct cb        **cb,
                                struct cb_region  *region,
                                cb_offset_t        cutoff_offset,
                                cb_offset_t       *header_offset)
{
    cb_offset_t           old_header_offset,
                          new_header_offset;
    struct cb_bst_header *old_header,
                         *new_header;
    int ret;

    old_header_offset = *header_offset;

    if (cb_bst_node_is_modifiable(old_header_offset, cutoff_offset))
        return 0;

    /* The provided header is unmodifiable and must be copied. */

    ret = cb_bst_header_alloc(cb, region, &new_header_offset);
    if (ret != 0)
        return ret;

    old_header = cb_bst_header_at(*cb, old_header_offset);
    new_header = cb_bst_header_at(*cb, new_header_offset);
    memcpy(new_header, old_header, sizeof(*new_header));

    *header_offset = new_header_offset;

    return 0;
}


static int
cb_bst_select_modifiable_node_raw(struct cb        **cb,
                                  struct cb_region  *region,
                                  cb_offset_t        cutoff_offset,
                                  cb_offset_t       *node_offset)
{
    /* If the node we are trying to modify has been freshly created, then it is
       safe to modify it in place.  Otherwise, a copy will be made and we will
       need to rewrite its parentage.  The cutoff_offset is the offset at which
       the unmodifiable to modifiable transition happens, with nodes prior
       to the offset requiring a new node to be allocated. */
    cb_offset_t         old_node_offset,
                        new_node_offset;
    int ret;

    old_node_offset = *node_offset;

    if (cb_bst_node_is_modifiable(old_node_offset, cutoff_offset))
        return 0;

    /*
     * The provided node is unmodifiable, so a new one is allocated and left
     * uninitialized.
     * */

    ret = cb_bst_node_alloc(cb, region, &new_node_offset);
    if (ret != 0)
        return ret;

    *node_offset = new_node_offset;

    return 0;
}


static int
cb_bst_select_modifiable_node(struct cb        **cb,
                              struct cb_region  *region,
                              cb_offset_t        cutoff_offset,
                              cb_offset_t       *node_offset)
{
    /* If the node we are trying to modify has been freshly created, then it is
       safe to modify it in place.  Otherwise, a copy will be made and we will
       need to rewrite its parentage.  The cutoff_offset is the offset at which
       the unmodifiable to modifiable transition happens, with nodes prior
       to the offset requiring a new node to be allocated. */
    cb_offset_t         old_node_offset,
                        new_node_offset;
    struct cb_bst_node *old_node,
                       *new_node;
    int ret;

    old_node_offset = *node_offset;

    if (cb_bst_node_is_modifiable(old_node_offset, cutoff_offset))
        return 0;

    /* The provided node is unmodifiable and must be copied. */

    ret = cb_bst_node_alloc(cb, region, &new_node_offset);
    if (ret != 0)
        return ret;

    old_node = cb_bst_node_at(*cb, old_node_offset);
    new_node = cb_bst_node_at(*cb, new_node_offset);
    memcpy(new_node, old_node, sizeof(*new_node));

    *node_offset = new_node_offset;

    return 0;
}


int
cb_bst_traverse(const struct cb        *cb,
                cb_offset_t             header_offset,
                cb_bst_traverse_func_t  func,
                void                   *closure)
{
    struct cb_bst_iter curr, end;
    int ret;

    cb_bst_get_iter_start(cb, header_offset, &curr);
    cb_bst_get_iter_end(cb, header_offset, &end);
    while (!cb_bst_iter_eq(&curr, &end))
    {
        ret = cb_bst_iter_visit(cb, &curr, func, closure);
        if (ret != 0)
            return ret;

        cb_bst_iter_next(cb, &curr);
    }

    return 0;
}


void
cb_bst_print(struct cb   **cb,
             cb_offset_t   header_offset)
{
    cb_offset_t           root_node_offset = CB_BST_SENTINEL;
    struct cb_bst_header *header;
    uint32_t              tree_height;

    if (header_offset == CB_BST_SENTINEL)
        return;

    header = cb_bst_header_at(*cb, header_offset);
    root_node_offset = header->root_node_offset;
    cb_assert(root_node_offset != CB_BST_SENTINEL);

    if (cb_bst_validate(cb, header_offset, NULL, NULL))
    {
        /* If we validated, then leverage structural check to print. */
        cb_bst_validate_structure(cb,
                                  root_node_offset,
                                  header->key_term_cmp,
                                  header->key_term_render,
                                  header->value_term_render,
                                  &tree_height,
                                  0,
                                  true,
                                  NULL);
    }
    else
    {
        /* Assume the failure has already printed */
        cb_log_error("BOGUS TREE");
    }
}


static int
cb_bst_red_pair_fixup_single(struct cb                  **cb,
                             struct cb_region            *region,
                             struct cb_bst_mutate_state  *s)
{
    /*
     *    grandparent 3,B         parent 2,B
     *                / \                / \
     *       parent 2,R  d    =>  curr 1,R 3,R
     *              / \                / \ / \
     *       curr 1,R  c              a  b c  d
     *            / \
     *           a   b
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                node1_offset,
                node2_offset,
                node3_offset;

    struct cb_bst_node *node2,
                       *node3;
    int ret;

    (void)ret;
    cb_log_debug("fixup_single @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check preconditions */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(s->grandparent_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_modifiable(s->grandparent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(s->grandparent_to_parent_dir == s->parent_to_curr_dir);

    /* Extract in-motion node offsets. */
    node1_offset  = s->curr_node_offset;
    node2_offset  = s->parent_node_offset;
    node3_offset  = s->grandparent_node_offset;
    c_node_offset = cb_bst_node_at(*cb, node2_offset)->child[!s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, node3_offset)->child[!s->grandparent_to_parent_dir];

    /* Select the nodes we'll be working with, known to already be modifiable. */
    ret = cb_bst_select_modifiable_node(cb, region, s->cutoff_offset, &node2_offset); //FIXME NODEALLOC DONE
    cb_assert(ret == 0);
    cb_assert(node2_offset == s->parent_node_offset);  //i.e. unchanged
    ret = cb_bst_select_modifiable_node(cb, region, s->cutoff_offset, &node3_offset); //FIXME NODEALLOC DONE
    cb_assert(ret == 0);
    cb_assert(node3_offset == s->grandparent_node_offset);  //i.e. unchanged

    /* Perform the fixup-single. */
    node2 = cb_bst_node_at(*cb, node2_offset);
    node2->color = CB_BST_BLACK;
    node2->child[s->parent_to_curr_dir] = node1_offset;
    node2->child[!s->parent_to_curr_dir] = node3_offset;

    node3 = cb_bst_node_at(*cb, node3_offset);
    node3->color = CB_BST_RED;
    node3->child[s->parent_to_curr_dir] = c_node_offset;
    node3->child[!s->parent_to_curr_dir] = d_node_offset;

    /* Maintain the mutation state. */
    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        cb_bst_node_at(*cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] = node2_offset;
    }
    if (s->new_root_node_offset == node3_offset)
        s->new_root_node_offset = node2_offset;
    s->grandparent_node_offset             = s->greatgrandparent_node_offset;
    s->grandparent_to_parent_dir           = s->greatgrandparent_to_grandparent_dir;
    s->parent_node_offset                  = node2_offset;
    s->greatgrandparent_node_offset        = CB_BST_SENTINEL; /* Unknown */
    s->greatgrandparent_to_grandparent_dir = -1;              /* Unknown */

    /* Check post-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[1]));

    return 0;
}


static int
cb_bst_red_pair_fixup_double(struct cb                  **cb,
                             struct cb_region            *region,
                             struct cb_bst_mutate_state  *s)
{
    /*
      grandparent 3,B         parent 2,B
                  / \                / \
         parent 1,R  d     =>      1,R 3,R
                / \                / \ / \   NOTE: curr is 1 or 3, depending
               a  2,R curr        a  b c  d        on dir.
                  / \
                 b   c
     */

    cb_offset_t a_node_offset,
                b_node_offset,
                c_node_offset,
                d_node_offset,
                node1_offset,
                node2_offset,
                node3_offset;

    struct cb_bst_node *node1,
                       *node2,
                       *node3;
    int ret;

    (void)ret;
    cb_log_debug("fixup_double @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check preconditions */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(s->grandparent_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_modifiable(s->grandparent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(s->grandparent_to_parent_dir != s->parent_to_curr_dir);

    /* Extract in-motion node offsets. */
    node1_offset  = s->parent_node_offset;
    node2_offset  = s->curr_node_offset;
    node3_offset  = s->grandparent_node_offset;
    a_node_offset = cb_bst_node_at(*cb, node1_offset)->child[!s->parent_to_curr_dir];
    b_node_offset = cb_bst_node_at(*cb, node2_offset)->child[!s->parent_to_curr_dir];
    c_node_offset = cb_bst_node_at(*cb, node2_offset)->child[s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, node3_offset)->child[!s->grandparent_to_parent_dir];

    /* Select the nodes we'll be working with, known to already be modifiable. */
    ret = cb_bst_select_modifiable_node(cb, region, s->cutoff_offset, &node1_offset); //FIXME NODEALLOC DONE
    cb_assert(ret == 0);
    cb_assert(node1_offset == s->parent_node_offset);
    ret = cb_bst_select_modifiable_node(cb, region, s->cutoff_offset, &node2_offset); //FIXME NODEALLOC DONE
    cb_assert(ret == 0);
    cb_assert(node2_offset == s->curr_node_offset);
    ret = cb_bst_select_modifiable_node(cb, region, s->cutoff_offset, &node3_offset); //FIXME NODEALLOC DONE
    cb_assert(ret == 0);
    cb_assert(node3_offset == s->grandparent_node_offset);

    /* Perform the fixup-double. */
    node1 = cb_bst_node_at(*cb, node1_offset);
    node1->color = CB_BST_RED;
    node1->child[!s->parent_to_curr_dir] = a_node_offset;
    node1->child[s->parent_to_curr_dir] = b_node_offset;

    node2 = cb_bst_node_at(*cb, node2_offset);
    node2->color = CB_BST_BLACK;
    node2->child[s->grandparent_to_parent_dir] = node1_offset;
    node2->child[!s->grandparent_to_parent_dir] = node3_offset;

    node3 = cb_bst_node_at(*cb, node3_offset);
    node3->color = CB_BST_RED;
    node3->child[s->grandparent_to_parent_dir] = c_node_offset;
    node3->child[!s->grandparent_to_parent_dir] = d_node_offset;

    /* Maintain the mutation state. */
    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        cb_bst_node_at(*cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] = node2_offset;
    }
    if (s->new_root_node_offset == node3_offset)
        s->new_root_node_offset = node2_offset;
    s->grandparent_node_offset   = s->greatgrandparent_node_offset;
    s->grandparent_to_parent_dir = s->greatgrandparent_to_grandparent_dir;
    s->parent_node_offset        = node2_offset;
    if (s->dir == s->parent_to_curr_dir)
    {
        s->curr_node_offset = node3_offset;
        s->dir              = !s->parent_to_curr_dir;
        /* s->parent_to_curr_dir remains same. */
    }
    else
    {
        s->curr_node_offset   = node1_offset;
        s->dir                = s->parent_to_curr_dir;
        s->parent_to_curr_dir = !s->parent_to_curr_dir;
    }
    s->greatgrandparent_node_offset        = CB_BST_SENTINEL; /* Unknown */
    s->greatgrandparent_to_grandparent_dir = -1;              /* Unknown */

    /* Check post-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[1]));

    return 0;
}

int
cb_bst_init(struct cb               **cb,
            struct cb_region         *region,
            cb_offset_t              *new_header_offset_out,
            cb_term_comparator_t      key_term_cmp,
            cb_term_comparator_t      value_term_cmp,
            cb_term_render_t          key_term_render,
            cb_term_render_t          value_term_render,
            cb_term_external_size_t   key_term_external_size,
            cb_term_external_size_t   value_term_external_size)
{
    struct cb_bst_header *header;
    int ret;

    ret = cb_bst_header_alloc(cb, region, new_header_offset_out);
    if (ret != 0)
        return ret;

    header = cb_bst_header_at(*cb, *new_header_offset_out);
    header->total_internal_size      = sizeof(struct cb_bst_header) + cb_alignof(struct cb_bst_header) - 1;
    header->total_external_size      = 0;
    header->hash_value               = 0;
    header->key_term_cmp             = key_term_cmp;
    header->value_term_cmp           = value_term_cmp;
    header->key_term_render          = key_term_render;
    header->value_term_render        = value_term_render;
    header->key_term_external_size   = key_term_external_size;
    header->value_term_external_size = value_term_external_size;
    header->root_node_offset         = CB_BST_SENTINEL;

    return 0;
}

/* NOTE: Insertion uses a top-down method. */
int
cb_bst_insert(struct cb            **cb,
              struct cb_region      *region,
              cb_offset_t           *header_offset,
              cb_offset_t            cutoff_offset,
              const struct cb_term  *key,
              const struct cb_term  *value)
{
    struct cb_bst_mutate_state  s = CB_BST_MUTATE_STATE_INIT;
    struct cb_bst_header       *header;
    cb_offset_t                 initial_cursor_offset = cb_cursor(*cb),
                                left_child_offset,
                                right_child_offset;
    struct cb_bst_node         *parent_node,
                               *curr_node,
                               *left_child_node,
                               *right_child_node,
                               *root_node;
    int                         cmp;
    ssize_t                     internal_size_adjust = 0;
    ssize_t                     external_size_adjust = 0;
    unsigned int                num_entries_adjust = 0;
    cb_hash_t                   hash_adjust = 0;
    cb_term_comparator_t        key_term_cmp;
    cb_term_external_size_t     key_term_external_size;
    cb_term_external_size_t     value_term_external_size;
    int ret;

    cb_log_debug("insert of key %s, value %s",
                 cb_term_to_str(cb, cb_bst_key_render_get(*cb, *header_offset), key),
                 cb_term_to_str(cb, cb_bst_value_render_get(*cb, *header_offset), value));

    /* Prepare a new header. */
    s.new_header_offset = *header_offset;
    if (s.new_header_offset == CB_BST_SENTINEL)
    {
        ret = cb_bst_init(cb,
                          region,
                          &s.new_header_offset,
                          &cb_term_cmp,
                          &cb_term_cmp,
                          &cb_term_render,
                          &cb_term_render,
                          &cb_term_external_size,
                          &cb_term_external_size);
        if (ret != 0)
            goto fail;

        header = cb_bst_header_at(*cb, s.new_header_offset);
    }
    else
    {
        ret = cb_bst_select_modifiable_header(cb,
                                              region,
                                              cutoff_offset,
                                              &s.new_header_offset);
        if (ret != 0)
            goto fail;

        header = cb_bst_header_at(*cb, s.new_header_offset);
    }

    /* Prepare the rest of the mutation state. */
    s.new_root_node_offset = header->root_node_offset;
    s.curr_node_offset     = s.new_root_node_offset;
    s.cutoff_offset        = cutoff_offset;

    /* Sample header contents now instead of continuously re-establishing header pointer. */
    key_term_cmp             = header->key_term_cmp;
    key_term_external_size   = header->key_term_external_size;
    value_term_external_size = header->value_term_external_size;

    cb_assert(cb_bst_mutate_state_validate(*cb, &s));
    cb_heavy_assert(cb_bst_validate(cb, *header_offset, "pre-insert", &s));

    /* Handle empty BSTs. */
    if (s.curr_node_offset == CB_BST_SENTINEL)
    {
        /* The tree is empty, insert a new black node. */
        ret = cb_bst_node_alloc(cb, region, &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        curr_node->color      = CB_BST_BLACK;
        curr_node->child[0]   = CB_BST_SENTINEL;
        curr_node->child[1]   = CB_BST_SENTINEL;
        cb_term_assign(&(curr_node->key), key);
        cb_term_assign(&(curr_node->value), value);
        curr_node->hash_value = cb_bst_node_hash(*cb, curr_node);

        header = cb_bst_header_at(*cb, s.new_header_offset);
        header->total_internal_size += (sizeof(struct cb_bst_node) + cb_alignof(struct cb_bst_node) - 1);
        header->total_external_size += (header->key_term_external_size(*cb, key) +
                                        header->value_term_external_size(*cb, value));
        header->num_entries      = 1;
        header->hash_value       ^= curr_node->hash_value;
        header->root_node_offset =  s.curr_node_offset;

        *header_offset = s.new_header_offset;

        cb_heavy_assert(cb_bst_validate(cb,
                                        *header_offset,
                                        "post-insert-success0",
                                        &s));
        return 0;
    }

    /* Begin path-copying downwards. */
    ret = cb_bst_select_modifiable_node(cb,
                                        region,
                                        cutoff_offset,
                                        &s.curr_node_offset);
    if (ret != 0)
        goto fail;

    s.new_root_node_offset = s.curr_node_offset;

    /* Skip parent_node update on first iteration, there is no parent. */
    goto entry;

    while (s.curr_node_offset != CB_BST_SENTINEL)
    {
        ret = cb_bst_select_modifiable_node(cb,
                                            region,
                                            cutoff_offset,
                                            &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
        parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;

entry:
        cb_assert(cb_bst_node_is_modifiable(s.curr_node_offset, cutoff_offset));
        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        cmp = key_term_cmp(*cb, key, &(curr_node->key));
        if (cmp == 0)
        {
            /* The key already exists in this tree.  Update the value at key
               and go no further. */

            /* Reduce by the old size and remove the old hash. */
            external_size_adjust -= (ssize_t)value_term_external_size(*cb,
                                                                      &(curr_node->value));
            hash_adjust ^= curr_node->hash_value;

            cb_term_assign(&(curr_node->value), value);
            curr_node->hash_value = cb_bst_node_hash(*cb, curr_node);

            /* Increment by the new size and add the new hash. */
            external_size_adjust += (ssize_t)value_term_external_size(*cb,
                                                                      &(curr_node->value));
            hash_adjust ^= curr_node->hash_value;

            goto done;
        }
        s.dir = (cmp == 1);

        left_child_offset  = curr_node->child[0];
        right_child_offset = curr_node->child[1];

        if (cb_bst_node_is_red(*cb, left_child_offset) &&
            cb_bst_node_is_red(*cb, right_child_offset))
        {
            cb_assert(curr_node->color == CB_BST_BLACK);

            ret = cb_bst_select_modifiable_node(cb,
                                                region,
                                                cutoff_offset,
                                                &left_child_offset);
            if (ret != 0)
                goto fail;

            ret = cb_bst_select_modifiable_node(cb,
                                                region,
                                                cutoff_offset,
                                                &right_child_offset);
            if (ret != 0)
                goto fail;

            /* Selecting modifiable nodes may have updated cb, so resample
               our pointers. */

            curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
            curr_node->color    = CB_BST_RED;
            curr_node->child[0] = left_child_offset;
            curr_node->child[1] = right_child_offset;

            left_child_node = cb_bst_node_at(*cb, left_child_offset);
            left_child_node->color = CB_BST_BLACK;

            right_child_node = cb_bst_node_at(*cb, right_child_offset);
            right_child_node->color = CB_BST_BLACK;

            if (cb_bst_node_is_red(*cb, s.parent_node_offset))
            {
                if (s.grandparent_to_parent_dir == s.parent_to_curr_dir)
                    ret = cb_bst_red_pair_fixup_single(cb, region, &s);
                else
                    ret = cb_bst_red_pair_fixup_double(cb, region, &s);

                if (ret != 0)
                    goto fail;
            }
        }

        /* Descend */
        cb_assert(s.grandparent_to_parent_dir == 0
               || s.grandparent_to_parent_dir == 1);
        s.greatgrandparent_to_grandparent_dir = s.grandparent_to_parent_dir;
        s.greatgrandparent_node_offset = s.grandparent_node_offset;

        cb_assert(s.parent_to_curr_dir == 0 || s.parent_to_curr_dir == 1);
        s.grandparent_to_parent_dir = s.parent_to_curr_dir;
        s.grandparent_node_offset = s.parent_node_offset;

        cb_assert(s.dir == 0 || s.dir == 1);
        s.parent_to_curr_dir = s.dir;
        s.parent_node_offset = s.curr_node_offset;

        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        s.curr_node_offset = curr_node->child[s.dir];
    }

    /* The key does not exist in the tree.  Insert a new red node. */

    cb_assert(s.curr_node_offset == CB_BST_SENTINEL);
    cb_assert(s.parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s.parent_to_curr_dir == 0 || s.parent_to_curr_dir == 1);

    ret = cb_bst_node_alloc(cb, region, &s.curr_node_offset);
    if (ret != 0)
        goto fail;

    /* Allocating a node may have updated cb, so resample our pointers. */

    parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
    parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;

    curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
    curr_node->color    = CB_BST_RED;
    curr_node->child[0] = CB_BST_SENTINEL;
    curr_node->child[1] = CB_BST_SENTINEL;
    cb_term_assign(&(curr_node->key), key);
    cb_term_assign(&(curr_node->value), value);
    curr_node->hash_value = cb_bst_node_hash(*cb, curr_node);

    internal_size_adjust = (ssize_t)(sizeof(struct cb_bst_node) + cb_alignof(struct cb_bst_node) - 1);
    external_size_adjust = (key_term_external_size(*cb, &(curr_node->key)) +
                            value_term_external_size(*cb, &(curr_node->value)));
    num_entries_adjust = 1;
    hash_adjust = curr_node->hash_value;

    if (cb_bst_node_is_red(*cb, s.parent_node_offset))
    {
        if (s.grandparent_to_parent_dir == s.parent_to_curr_dir)
            ret = cb_bst_red_pair_fixup_single(cb, region, &s);
        else
            ret = cb_bst_red_pair_fixup_double(cb, region, &s);

        if (ret != 0)
            goto fail;
    }

done:
    root_node = cb_bst_node_at(*cb, s.new_root_node_offset);
    root_node->color = CB_BST_BLACK;

    header = cb_bst_header_at(*cb, s.new_header_offset);
    header->total_internal_size += internal_size_adjust;
    header->total_external_size += external_size_adjust;
    header->num_entries         += num_entries_adjust;
    header->hash_value          ^= hash_adjust;
    header->root_node_offset    =  s.new_root_node_offset;

    *header_offset = s.new_header_offset;

    cb_heavy_assert(cb_bst_validate(cb,
                                    *header_offset,
                                    "post-insert-success",
                                    &s));
    return 0;

fail:
    cb_rewind_to(*cb, initial_cursor_offset);
    cb_heavy_assert(cb_bst_validate(cb,
                                    *header_offset,
                                    "post-insert-fail",
                                    &s));
    return ret;
}


static int
cb_bst_delete_fix_root(struct cb                  **cb,
                       struct cb_region            *region,
                       struct cb_bst_mutate_state  *s)
{

    /*
            curr 2,R               parent 3,R
            dir /   \ !dir               /   \
              1,B   3,R     =>    curr 2,R    d
              / \   / \                / \
             a   b c   d             1,B  c
                                     / \
                                    a   b
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                node1_offset,
                node2_offset,
                old_node3_offset,
                new_node3_offset;

    struct cb_bst_node *node2,
                       *old_node3,
                       *new_node3;
    int ret;

    cb_log_debug("fixroot @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(s->curr_node_offset == s->new_root_node_offset);
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir]));
    cb_assert(cb_bst_node_is_red(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir]));

    node1_offset     = cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir];
    node2_offset     = s->curr_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir];

    (void)node1_offset;
    cb_assert(cb_bst_node_is_black(*cb, node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, node2_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node3_offset));

    c_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[s->dir];
    d_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[!s->dir];

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, region, &new_node3_offset);
    if (ret != 0)
        return ret;

    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    cb_assert(cb_bst_node_is_modifiable(node2_offset, s->cutoff_offset));
    cb_assert(node2->child[s->dir] == node1_offset);
    node2->child[!s->dir] = c_node_offset;

    cb_term_assign(&(new_node3->key), &(old_node3->key));
    cb_term_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->dir] = node2_offset;
    new_node3->child[!s->dir] = d_node_offset;

    cb_assert(s->new_root_node_offset == node2_offset);
    s->new_root_node_offset = new_node3_offset;

    /* Maintain iterator. */
    s->parent_node_offset        = new_node3_offset;
    s->parent_to_curr_dir        = s->dir;
    s->sibling_node_offset       = d_node_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case1(struct cb                  **cb,
                    struct cb_region            *region,
                    struct cb_bst_mutate_state  *s)
{
    /*
     *      parent 4,R                     grandparent 4,R
     *            /   \                                / \
     *     curr 2,B   e,B sibling             parent 3,B e,B
     *         /   \                 =>              / \
     *    dir /     \ !dir                    curr 2,R d,B sibling
     *      1,B     3,R                        dir / \ !dir
     *      / \     / \                          1,B  c,B
     *     a   b  c,B d,B                        / \
     *                                          a   b
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                e_node_offset,
                node1_offset,
                node2_offset,
                node4_offset,
                old_node3_offset,
                new_node3_offset;

    struct cb_bst_node *node2,
                       *node4,
                       *old_node3,
                       *new_node3;

    int ret;

    cb_log_debug("delete case1 @ curr_node_offset: %ju",
                 (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(s->parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir])->child[1]));

    /* Extract in-motion node offsets. */
    node1_offset     = cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir];
    node2_offset     = s->curr_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir];
    node4_offset     = s->parent_node_offset;
    c_node_offset    = cb_bst_node_at(*cb, old_node3_offset)->child[s->dir];
    d_node_offset    = cb_bst_node_at(*cb, old_node3_offset)->child[!s->dir];
    e_node_offset    = cb_bst_node_at(*cb, node4_offset)->child[!s->parent_to_curr_dir];

    /* Obtain suitable node for writing. */
    new_node3_offset = old_node3_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node3_offset);
    if (ret != 0)
        return ret;

    /* Perform the delete case 1. */
    node2 = cb_bst_node_at(*cb, node2_offset);
    node4 = cb_bst_node_at(*cb, node4_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    node2->color = CB_BST_RED;
    node2->child[s->dir] = node1_offset;
    node2->child[!s->dir] = c_node_offset;

    new_node3->color = CB_BST_BLACK;
    new_node3->child[s->dir] = node2_offset;
    new_node3->child[!s->dir] = d_node_offset;
    cb_term_assign(&(new_node3->key), &(old_node3->key));
    cb_term_assign(&(new_node3->value), &(old_node3->value));

    node4->color = CB_BST_RED;
    node4->child[s->parent_to_curr_dir] = new_node3_offset;
    node4->child[!s->parent_to_curr_dir] = e_node_offset;

    /* Maintain the mutation iterator state. */
    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = node4_offset;

    s->grandparent_node_offset   = node4_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->parent_node_offset        = new_node3_offset;
    s->parent_to_curr_dir        = s->dir;
    s->curr_node_offset          = node2_offset;
    s->sibling_node_offset       = cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir];

    /* Check post-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    /* e is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir]));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    /* d is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case2(struct cb                  **cb,
                    struct cb_region            *region,
                    struct cb_bst_mutate_state  *s)
{

    /*
     *       parent 2,R                        grandparent 3,R
     *             /   \                                  /   \
     *            /     \                                /     \
     *     curr 1,B      4,B sibling     =>     parent 2,B     4,B
     *          / \      / \                           / \     / \
     *        a,B b,B  3,R  e                   curr 1,R c,B d,B  e
     *                 / \                           / \
     *               c,B d,B                       a,B b,B
     *
     *                                          (c is sibling)
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                e_node_offset,
                node1_offset,
                node2_offset,
                old_node3_offset,
                old_node4_offset,
                new_node3_offset,
                new_node4_offset;

    struct cb_bst_node *node1,
                       *node2,
                       *old_node3,
                       *old_node4,
                       *new_node3,
                       *new_node4;
    int ret;

    cb_log_debug("delete case2 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(s->parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir])->child[1]));

    /* Extract in-motion node offsets. */
    node1_offset     = s->curr_node_offset;
    node2_offset     = s->parent_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir];
    old_node4_offset = s->sibling_node_offset;
    c_node_offset    = cb_bst_node_at(*cb, old_node3_offset)->child[s->parent_to_curr_dir];
    d_node_offset    = cb_bst_node_at(*cb, old_node3_offset)->child[!s->parent_to_curr_dir];
    e_node_offset    = cb_bst_node_at(*cb, old_node4_offset)->child[!s->parent_to_curr_dir];

    /* Obtain suitable nodes for rewrite, traversal-contiguous. */
    new_node3_offset = old_node3_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node3_offset);
    if (ret != 0)
        return ret;
    new_node4_offset = old_node4_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node4_offset);
    if (ret != 0)
        return ret;

    /* Perform the delete case 2. */
    node1     = cb_bst_node_at(*cb, node1_offset);
    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    old_node4 = cb_bst_node_at(*cb, old_node4_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);
    new_node4 = cb_bst_node_at(*cb, new_node4_offset);

    node1->color = CB_BST_RED;

    node2->color = CB_BST_BLACK;
    node2->child[!s->parent_to_curr_dir] = c_node_offset;

    cb_term_assign(&(new_node3->key), &(old_node3->key));
    cb_term_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->parent_to_curr_dir] = node2_offset;
    new_node3->child[!s->parent_to_curr_dir] = new_node4_offset;

    cb_term_assign(&(new_node4->key), &(old_node4->key));
    cb_term_assign(&(new_node4->value), &(old_node4->value));
    new_node4->color = CB_BST_BLACK;
    new_node4->child[s->parent_to_curr_dir] = d_node_offset;
    new_node4->child[!s->parent_to_curr_dir] = e_node_offset;

    /* Maintain the mutation iterator state. */
    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = new_node3_offset;
    if (s->new_root_node_offset == node2_offset)
        s->new_root_node_offset = new_node3_offset;
    s->grandparent_node_offset   = new_node3_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->sibling_node_offset       = c_node_offset;

    /* Check post-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    /* 4 is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir]));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    /* c is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    /* d is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir])->child[s->grandparent_to_parent_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case4(struct cb                  **cb,
                    struct cb_region            *region,
                    struct cb_bst_mutate_state  *s)
{
    /*
     *      parent 2,R                      grandparent 3,R
     *            /   \                                /   \
     *           /     \                              /     \
     *    curr 1,B     3,B sibling    =>     parent 2,B     4,B
     *         / \     / \                          / \     / \
     *       a,B b,B c,B 4,R                 curr 1,R c,B d,B e,B
     *                   / \                      / \
     *                 d,B e,B                  a,B b,B
     *
     *                                       (c is sibling)
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                e_node_offset,
                node1_offset,
                node2_offset,
                old_node3_offset,
                old_node4_offset,
                new_node3_offset,
                new_node4_offset;

    struct cb_bst_node *node1,
                       *node2,
                       *old_node3,
                       *old_node4,
                       *new_node3,
                       *new_node4;
    int ret;

    cb_log_debug("delete case4 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(s->parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir])->child[1]));

    /* Extract in-motion node offsets. */
    node1_offset     = s->curr_node_offset;
    node2_offset     = s->parent_node_offset;
    old_node3_offset = s->sibling_node_offset;
    old_node4_offset = cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir];
    c_node_offset    = cb_bst_node_at(*cb, old_node3_offset)->child[s->parent_to_curr_dir];
    d_node_offset    = cb_bst_node_at(*cb, old_node4_offset)->child[s->parent_to_curr_dir];
    e_node_offset    = cb_bst_node_at(*cb, old_node4_offset)->child[!s->parent_to_curr_dir];

    /* Obtain suitable nodes for rewrite, traversal-contiguous. */
    new_node3_offset = old_node3_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node3_offset);
    if (ret != 0)
        return ret;
    new_node4_offset = old_node4_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node4_offset);
    if (ret != 0)
        return ret;

    /* Perform the delete case 4. */
    node1     = cb_bst_node_at(*cb, node1_offset);
    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    old_node4 = cb_bst_node_at(*cb, old_node4_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);
    new_node4 = cb_bst_node_at(*cb, new_node4_offset);

    node1->color = CB_BST_RED;

    node2->color = CB_BST_BLACK;
    node2->child[!s->parent_to_curr_dir] = c_node_offset;

    cb_term_assign(&(new_node3->key), &(old_node3->key));
    cb_term_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->parent_to_curr_dir] = node2_offset;
    new_node3->child[!s->parent_to_curr_dir] = new_node4_offset;

    cb_term_assign(&(new_node4->key), &(old_node4->key));
    cb_term_assign(&(new_node4->value), &(old_node4->value));
    new_node4->color = CB_BST_BLACK;
    new_node4->child[s->parent_to_curr_dir] = d_node_offset;
    new_node4->child[!s->parent_to_curr_dir] = e_node_offset;

    /* Maintain the mutation iterator state. */
    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = new_node3_offset;
    if (s->new_root_node_offset == node2_offset)
        s->new_root_node_offset = new_node3_offset;
    s->grandparent_node_offset   = new_node3_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->sibling_node_offset       = c_node_offset;

    /* Check post-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    /* 4 is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir]));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    /* d is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir])->child[s->parent_to_curr_dir]));
    /* e is black. */
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir])->child[s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case5(struct cb                  **cb,
                    struct cb_region            *region,
                    struct cb_bst_mutate_state  *s)
{
    /*
     *         parent 3,R                        parent 3,B
     *               /   \                             /   \
     *              /     \                           /     \
     *       curr 1,B     5,B sibling    =>    curr 1,R     5,R sibling
     *            / \     / \                       / \     / \
     *          0,B 2,B 4,B 6,B                   0,B 2,B 4,B 6,B
     *          / \ / \ / \ / \                   / \ / \ / \ / \
     *          a b c d e f g h                   a b c d e f g h
     */

    cb_offset_t node1_offset,
                node3_offset,
                old_node5_offset,
                new_node5_offset;

    struct cb_bst_node *node1,
                       *node3,
                       *old_node5,
                       *new_node5;
    int ret;

    cb_log_debug("delete case5 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions. */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_modifiable(s->parent_node_offset, s->cutoff_offset));
    cb_assert(cb_bst_node_is_modifiable(s->curr_node_offset, s->cutoff_offset));
    cb_assert(s->parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[1]));

    /* Extract in-motion node offsets. */
    node1_offset     = s->curr_node_offset;
    node3_offset     = s->parent_node_offset;
    old_node5_offset = s->sibling_node_offset;

    /* Obtain suitable nodes for rewrite, traversal-contiguous. */
    new_node5_offset = old_node5_offset;
    ret = cb_bst_select_modifiable_node_raw(cb,
                                            region,
                                            s->cutoff_offset,
                                            &new_node5_offset);
    if (ret != 0)
        return ret;

    /* Perform the delete case 5. */
    node1     = cb_bst_node_at(*cb, node1_offset);
    node3     = cb_bst_node_at(*cb, node3_offset);
    old_node5 = cb_bst_node_at(*cb, old_node5_offset);
    new_node5 = cb_bst_node_at(*cb, new_node5_offset);

    node1->color = CB_BST_RED;

    node3->color = CB_BST_BLACK;
    node3->child[!s->parent_to_curr_dir] = new_node5_offset;

    cb_term_assign(&(new_node5->key), &(old_node5->key));
    cb_term_assign(&(new_node5->value), &(old_node5->value));
    new_node5->color = CB_BST_RED;
    new_node5->child[0] = old_node5->child[0];
    new_node5->child[1] = old_node5->child[1];

    /* Maintain the mutation iterator state. */
    s->sibling_node_offset = new_node5_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_mutate_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[1]));

    return 0;
}


int
cb_bst_delete(struct cb            **cb,
              struct cb_region      *region,
              cb_offset_t           *header_offset,
              cb_offset_t            cutoff_offset,
              const struct cb_term  *key)
{
    struct cb_bst_mutate_state  s = CB_BST_MUTATE_STATE_INIT;
    struct cb_bst_header       *header;
    cb_offset_t                 initial_cursor_offset = cb_cursor(*cb),
                                found_node_offset = CB_BST_SENTINEL;
    struct cb_bst_node         *root_node,
                               *parent_node,
                               *curr_node,
                               *found_node;
    int                         cmp;
    size_t                      internal_size_subtract = 0;
    size_t                      external_size_subtract = 0;
    unsigned int                num_entries_adjust = 0;
    cb_hash_t                   hash_adjust = 0;
    int ret;

    cb_log_debug("delete of key %s", cb_term_to_str(cb, cb_bst_key_render_get(*cb, *header_offset), key));

    s.new_header_offset = *header_offset;

    /* For trees not containing the key, there is nothing to do. */
    /* NOTE: We have two options here:
     * 1) Simply check if the key exists before we proceed with the modify-in-
     *    place approach to deletion using cb_bst_select_modifiable_node(),
     *    aborting the deletion request if the key does not exist.
     * 2) Disallow reuse of existing nodes provided by
     *    cb_bst_select_modifiable_node() and always speculatively copy the
     *    path until we know whether the key exists or not.  If the key doesn't
     *    exist, rewind to discard this new path.  If the key does exist, commit
     *    by modifying the passed-in header_node_offset.  This prevents bogus,
     *    unrewindable modifications to the tree for missing keys.
     * Option 1 provides a denser tree at the cost of a double traversal.  It is
     * presently the option we use.
     */
    if (!cb_bst_contains_key(*cb, s.new_header_offset, key))
    {
        ret = -1;
        goto fail;
    }
    num_entries_adjust = 1;

    /* Prepare a new header. */
    ret = cb_bst_select_modifiable_header(cb,
                                          region,
                                          cutoff_offset,
                                          &s.new_header_offset);
    if (ret != 0)
        goto fail;

    header = cb_bst_header_at(*cb, s.new_header_offset);
    cb_assert(header->root_node_offset != CB_BST_SENTINEL);


    /* Prepare the rest of the mutation state. */
    s.new_root_node_offset = header->root_node_offset;
    s.curr_node_offset     = s.new_root_node_offset;
    s.cutoff_offset        = cutoff_offset;


    cb_assert(cb_bst_mutate_state_validate(*cb, &s));
    cb_heavy_assert(cb_bst_validate(cb, *header_offset, "pre-delete", &s));


    /* Begin path-copying downwards. */
    ret = cb_bst_select_modifiable_node(cb,
                                        region,
                                        cutoff_offset,
                                        &s.curr_node_offset);
    if (ret != 0)
        goto fail;

    curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
    curr_node->color = CB_BST_RED;
    s.new_root_node_offset = s.curr_node_offset;
    cmp = header->key_term_cmp(*cb, key, &(curr_node->key));
    if (cmp == 0)
    {
        /* The key-to-delete exists in this tree.  Make note of this offset
           as we'll rewrite the key/value of this node once we have it's
           in-order successor or predecessor. */
        found_node_offset = s.curr_node_offset;

        /* Descend if there are children, preferring predecessors. */
        cmp = (curr_node->child[0] != CB_BST_SENTINEL) ? -1 : 1;
    }
    s.dir = (cmp == 1);

    if (cb_bst_node_is_black(*cb, curr_node->child[s.dir]) &&
        cb_bst_node_is_red(*cb, curr_node->child[!s.dir]))
    {
        ret = cb_bst_delete_fix_root(cb, region, &s);
        if (ret != 0)
            goto fail;
    }

    /* Skip parent_node update on first iteration, there is no parent. */
    goto entry;

    while (s.curr_node_offset != CB_BST_SENTINEL)
    {
        ret = cb_bst_select_modifiable_node(cb,
                                            region,
                                            cutoff_offset,
                                            &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
        parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;

        /* Our parent should always be red, unless curr itself is red. */
        cb_assert(cb_bst_node_is_red(*cb, s.parent_node_offset) ||
               cb_bst_node_is_red(*cb, s.curr_node_offset));

        cb_assert(cb_bst_node_is_modifiable(s.curr_node_offset, cutoff_offset));
        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        cmp = header->key_term_cmp(*cb, key, &(curr_node->key));
        if (cmp == 0)
        {
            /* The key-to-delete exists in this tree.  Make note of this offset
               as we'll rewrite the key/value of this node once we have it's
               in-order successor or predecessor. */
            found_node_offset = s.curr_node_offset;

            /* Descend if there are children, preferring predecessors. */
            cmp = (curr_node->child[0] != CB_BST_SENTINEL) ? 0 : 1;
        }
        s.dir = (cmp == 1);

entry:
        /* CASE 0a - "Current is Red" */
        if (curr_node->color == CB_BST_RED)
        {
            cb_log_debug("delete case0a @ %ju", (uintmax_t)s.curr_node_offset);
            goto descend;
        }

        /* CASE 0b - "Child-To-Descend-To is Red" */
        cb_assert(curr_node->color == CB_BST_BLACK);
        if (cb_bst_node_is_red(*cb, curr_node->child[s.dir]))
        {
            cb_log_debug("delete case0b @ %ju", (uintmax_t)s.curr_node_offset);
            goto descend;
        }

        /* CASE 1 - "Child-To-Descend-To's Sibling is Red */
        if (cb_bst_node_is_red(*cb, curr_node->child[!s.dir]))
        {
            ret = cb_bst_delete_case1(cb, region, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 2  - "Current's Near Nephew is Red" */
        /*
         * Since curr is a non-CB_BST_SENTINEL black node, it must have a
         * non-CB_BST_SENTINEL sibling.
         */
        s.sibling_node_offset =
            cb_bst_node_at(*cb, s.parent_node_offset)->child[!s.parent_to_curr_dir];
        cb_assert(s.sibling_node_offset != CB_BST_SENTINEL);
        if (cb_bst_node_is_red(*cb,
                cb_bst_node_at(*cb, s.sibling_node_offset)->child[s.parent_to_curr_dir]))
        {
            ret = cb_bst_delete_case2(cb, region, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 4 - "Current's Far Nephew is Red" */
        cb_assert(s.sibling_node_offset != CB_BST_SENTINEL);
        if (cb_bst_node_is_red(*cb,
                cb_bst_node_at(*cb, s.sibling_node_offset)->child[!s.parent_to_curr_dir]))
        {
            ret = cb_bst_delete_case4(cb, region, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 5 - "Sibling and Its Children are Black" */
        if (cb_bst_node_is_black(*cb, s.sibling_node_offset))
        {
            ret = cb_bst_delete_case5(cb, region, &s);
            if (ret != 0)
                goto fail;
        }

        /* The following arrangement can never happen. */
        cb_assert(!(cb_bst_node_is_black(*cb, s.curr_node_offset) &&
                    cb_bst_node_is_red(*cb, s.parent_node_offset)));

descend:
        cb_assert(s.parent_to_curr_dir == 0 || s.parent_to_curr_dir == 1);
        s.grandparent_to_parent_dir = s.parent_to_curr_dir;
        s.grandparent_node_offset = s.parent_node_offset;

        cb_assert(s.dir == 0 || s.dir == 1);
        cb_assert(s.curr_node_offset != CB_BST_SENTINEL);
        s.parent_to_curr_dir = s.dir;
        s.parent_node_offset = s.curr_node_offset;

        s.curr_node_offset =
            cb_bst_node_at(*cb, s.curr_node_offset)->child[s.dir];

        s.sibling_node_offset =
            cb_bst_node_at(*cb, s.parent_node_offset)->child[!s.parent_to_curr_dir];
    }


    /* OPTIM We break the loop at a sentinel, having unnecessarily created
      a modifiable copy of the preceding red "leaf", only to remove it.
      Optimize this? */
    if (found_node_offset == CB_BST_SENTINEL)
    {
        /* If we haven't found a node with the given key, abort the work we
           did, including the creation of a new node path, by rewinding the
           cursor. */
        ret = -1;
        goto fail;
    }
    found_node = cb_bst_node_at(*cb, found_node_offset);
    internal_size_subtract = (sizeof(struct cb_bst_node) + cb_alignof(struct cb_bst_node) - 1);
    external_size_subtract = (header->key_term_external_size(*cb, &(found_node->key)) +
                              header->value_term_external_size(*cb, &(found_node->value)));
    hash_adjust   = found_node->hash_value;

    cb_assert(s.parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s.curr_node_offset == CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s.parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s.grandparent_node_offset) ||
           s.grandparent_node_offset == s.new_root_node_offset);

    if (found_node_offset != s.parent_node_offset)
    {
        struct cb_bst_node *to_delete_node;

        to_delete_node = cb_bst_node_at(*cb, s.parent_node_offset);
        cb_term_assign(&(found_node->key), &(to_delete_node->key));
        cb_term_assign(&(found_node->value), &(to_delete_node->value));
    }

    if (s.grandparent_node_offset != CB_BST_SENTINEL)
    {
        struct cb_bst_node *grandparent_node;

        cb_assert(cb_bst_node_is_modifiable(s.grandparent_node_offset,
                                            cutoff_offset));
        grandparent_node = cb_bst_node_at(*cb, s.grandparent_node_offset);
        cb_assert(grandparent_node->child[s.grandparent_to_parent_dir] ==
               s.parent_node_offset);
        grandparent_node->child[s.grandparent_to_parent_dir] =
            CB_BST_SENTINEL;
    }

    if (s.parent_node_offset == s.new_root_node_offset)
    {
        cb_log_debug("assigning CB_BST_SENTINEL to root-node");
        s.new_root_node_offset = CB_BST_SENTINEL;
    }
    else
    {
        cb_log_debug("assigning black to root-node @ %ju",
                     s.new_root_node_offset);
        root_node = cb_bst_node_at(*cb, s.new_root_node_offset);
        root_node->color = CB_BST_BLACK;
    }

    header = cb_bst_header_at(*cb, s.new_header_offset);
    cb_assert(internal_size_subtract < header->total_internal_size);
    cb_assert(external_size_subtract < header->total_external_size);
    header->total_internal_size -= internal_size_subtract;
    header->total_external_size -= external_size_subtract;
    header->num_entries         -= num_entries_adjust;
    header->hash_value          ^= hash_adjust;
    header->root_node_offset    =  s.new_root_node_offset;

    cb_heavy_assert(cb_bst_validate(cb,
                                    *header_offset,
                                    "post-delete-success",
                                    &s));

    return 0;

fail:
    cb_rewind_to(*cb, initial_cursor_offset);
    cb_assert(ret != 0); /* ret == 0 implies an implementation error. */
    cb_heavy_assert(cb_bst_validate(cb,
                                    *header_offset,
                                    "post-delete-fail",
                                    &s));
    return ret;
}


int
cb_bst_cmp(const struct cb      *cb,
           cb_offset_t           lhs_header_offset,
           cb_offset_t           rhs_header_offset)
{
    struct cb_bst_header *lhs_header;
    struct cb_bst_header *rhs_header;

    struct cb_bst_iter lhs_curr,
                       lhs_end,
                       rhs_curr,
                       rhs_end;
    struct cb_term     lhs_key,
                       lhs_value,
                       rhs_key,
                       rhs_value;
    int                cmp;

    lhs_header = cb_bst_header_at(cb, lhs_header_offset);
    rhs_header = cb_bst_header_at(cb, rhs_header_offset);

    //NOTE: It is not sensible to compare BSTs using different comparators
    // (which would we prefer for *this* comparison?).  It must be expected
    //  that the user has segregated their BST uses appropriately such that
    //  only like-comparator ones are ever compared.
    cb_assert(lhs_header->key_term_cmp == rhs_header->key_term_cmp);
    cb_assert(lhs_header->value_term_cmp == rhs_header->value_term_cmp);
    (void)rhs_header;

    cb_bst_get_iter_start(cb, lhs_header_offset, &lhs_curr);
    cb_bst_get_iter_end(cb, lhs_header_offset, &lhs_end);
    cb_bst_get_iter_start(cb, rhs_header_offset, &rhs_curr);
    cb_bst_get_iter_end(cb, rhs_header_offset, &rhs_end);

    while (!cb_bst_iter_eq(&lhs_curr, &lhs_end) &&
           !cb_bst_iter_eq(&rhs_curr, &rhs_end))
    {

        cb_bst_iter_deref(cb, &lhs_curr, &lhs_key, &lhs_value);
        cb_bst_iter_deref(cb, &rhs_curr, &rhs_key, &rhs_value);

        cmp = lhs_header->key_term_cmp(cb, &lhs_key, &rhs_key);
        if (cmp != 0)
            return cmp;

        cmp = lhs_header->value_term_cmp(cb, &lhs_value, &rhs_value);
        if (cmp != 0)
            return cmp;

        cb_bst_iter_next(cb, &lhs_curr);
        cb_bst_iter_next(cb, &rhs_curr);
    }

    if (cb_bst_iter_eq(&lhs_curr, &lhs_end))
    {
        if (cb_bst_iter_eq(&rhs_curr, &rhs_end))
            return 0;  /* lhs equal to rhs*/

        return -1; /* lhs less than rhs */
    }

    cb_assert(cb_bst_iter_eq(&rhs_curr, &rhs_end));
    return 1; /* lhs greater than rhs */
}

size_t
cb_bst_internal_size_given_key_count(unsigned int keys) {

    return (sizeof(struct cb_bst_header) + cb_alignof(struct cb_bst_header) - 1)
        + keys * (sizeof(struct cb_bst_node) + cb_alignof(struct cb_bst_node) - 1);
}

size_t
cb_bst_internal_size(const struct cb *cb,
                     cb_offset_t      header_offset)
{
    if (header_offset == CB_BST_SENTINEL)
        return 0;

    return cb_bst_header_at(cb, header_offset)->total_internal_size;
}

size_t
cb_bst_external_size(const struct cb *cb,
                     cb_offset_t      header_offset)
{
    if (header_offset == CB_BST_SENTINEL)
        return 0;

    return cb_bst_header_at(cb, header_offset)->total_external_size;
}

int
cb_bst_external_size_adjust(struct cb   *cb,
                            cb_offset_t  header_offset,
                            ssize_t      adjustment)
{
    if (header_offset == CB_BST_SENTINEL) {
        return -1;
    }

    struct cb_bst_header *header = cb_bst_header_at(cb, header_offset);
    header->total_external_size = (size_t)((ssize_t)header->total_external_size + adjustment);
    return 0;
}

size_t
cb_bst_size(const struct cb *cb,
            cb_offset_t      header_offset)
{
    if (header_offset == CB_BST_SENTINEL)
        return 0;

    return cb_bst_header_at(cb, header_offset)->total_internal_size +
           cb_bst_header_at(cb, header_offset)->total_external_size;
}

unsigned int
cb_bst_num_entries(const struct cb *cb,
                   cb_offset_t      header_offset)
{
    if (header_offset == CB_BST_SENTINEL)
        return 0;

    return cb_bst_header_at(cb, header_offset)->num_entries;
}

void
cb_bst_hash_continue(cb_hash_state_t *hash_state,
                     const struct cb *cb,
                     cb_offset_t      header_offset)
{
    cb_hash_t hash_value = 0;

    if (header_offset != CB_BST_SENTINEL)
        hash_value = cb_bst_header_at(cb, header_offset)->hash_value;

    cb_hash_continue(hash_state, &hash_value, sizeof(hash_value));
}


cb_hash_t
cb_bst_hash(const struct cb *cb,
            cb_offset_t      header_offset)
{
    cb_hash_state_t hash_state;

    cb_hash_init(&hash_state);
    cb_bst_hash_continue(&hash_state, cb, header_offset);

    return cb_hash_finalize(&hash_state);
}


static int
cb_bst_render_node(cb_offset_t   *dest_offset,
                   struct cb    **cb,
                   cb_offset_t    node_offset,
                   unsigned int   flags)
{
    /*
     * FIXME this is a very crap implementation which probably has O(n^2) or
     * worse performance due to it rendering the child substrings before
     * rendering the enclosing parent using them. Recursive would be better,
     * and stack-based recursion best here.
     */
    cb_offset_t         orig_cursor_pos = cb_cursor(*cb);
    struct cb_bst_node *node;
    cb_offset_t         str_offset,
                        keystr_offset,
                        valstr_offset,
                        leftstr_offset,
                        rightstr_offset;
    const char         *str,
                       *keystr   = "(render error)",
                       *valstr   = "(render error)",
                       *leftstr  = "(render error)",
                       *rightstr = "(render error)";
    char               *final_dest;
    size_t              str_size; /* Including null terminator. */
    int ret;


    node = cb_bst_node_at(*cb, node_offset);
    if (!node)
        return cb_asprintf(dest_offset, cb, "NIL");

    ret = cb_term_render(&keystr_offset, cb, &(node->key), flags);
    if (ret == 0)
        keystr = (const char*)cb_at(*cb, keystr_offset);

    ret = cb_term_render(&valstr_offset, cb, &(node->value), flags);
    if (ret == 0)
        valstr = (const char*)cb_at(*cb, valstr_offset);

    ret = cb_bst_render_node(&leftstr_offset, cb, node->child[0], flags);
    if (ret == 0)
        leftstr = (const char*)cb_at(*cb, leftstr_offset);

    ret = cb_bst_render_node(&rightstr_offset, cb, node->child[1], flags);
    if (ret == 0)
        rightstr = (const char*)cb_at(*cb, rightstr_offset);

    ret = cb_asprintf(&str_offset, cb, "(%s %s=%s %s)",
                      leftstr,
                      keystr,
                      valstr,
                      rightstr);
    if (ret != 0)
        goto fail;

    /*
     * Now we have a rendered string for this BST node, existing subsequent in
     * the continuous buffer to its rendered children.  These children
     * renderings are now useless data.  We shift the parent rendered string
     * over this earlier garbage to not be too wasteful with memory.  If the
     * rewind + ensure_free_contiguous would cause a grow of the continous
     * buffer the originally written string will not be copied to this new
     * continuous buffer (because it no longer existing as part of the active
     * data due to the rewind).  However, we will have cached the string where
     * it still exists in the smaller, earlier version of the continuous buffer
     * (this owning continuous buffer of which will get lazily removed at some
     * safe point in the future).
     */

    str      = (const char *)cb_at(*cb, str_offset);
    str_size = strlen(str) + 1;

    cb_rewind_to(*cb, orig_cursor_pos);
    ret = cb_ensure_free_contiguous(cb, str_size);
    if (ret != 0)
        goto fail;

    final_dest = (char*)cb_at(*cb, cb_cursor(*cb));
    memmove(final_dest, str, str_size);
    *dest_offset = cb_cursor(*cb);
    cb_cursor_advance(*cb, str_size);
    return 0;

fail:
    cb_rewind_to(*cb, orig_cursor_pos);
    return -1;
}


int
cb_bst_render(cb_offset_t   *dest_offset,
              struct cb    **cb,
              cb_offset_t    header_offset,
              unsigned int   flags)
{
    struct cb_bst_header *header;

    if (header_offset == CB_BST_SENTINEL)
        return cb_asprintf(dest_offset, cb, "NIL");

    header = cb_bst_header_at(*cb, header_offset);
    cb_assert(header->root_node_offset != CB_BST_SENTINEL);

    return cb_bst_render_node(dest_offset, cb, header->root_node_offset, flags);
}


const char*
cb_bst_to_str(struct cb   **cb,
              cb_offset_t   header_offset)
{
    cb_offset_t dest_offset;
    int ret;

    ret = cb_bst_render(&dest_offset, cb, header_offset, CB_RENDER_DEFAULT);
    if (ret != 0)
        return "(render-error)";

    return (const char*)cb_at(*cb, dest_offset);
}
