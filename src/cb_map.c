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
#include "cb_map.h"

#include <assert.h>
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


enum cb_command_type
{
    CB_CMD_START_DATA,
    CB_CMD_KEYVAL,
    CB_CMD_DELETEKEY,
    CB_CMD_BST,
    CB_CMD_RBTREE,
    CB_CMD_MAX
};


struct cb_command_keyval
{
    struct cb_key    key;
    struct cb_value  value;
};


struct cb_command_deletekey
{
    struct cb_key  key;
};


struct cb_command_bst
{
    cb_offset_t  root_node_offset;
};


struct cb_bst_node
{
    struct cb_key    key;
    struct cb_value  value;
    unsigned int     color;
    cb_offset_t      child[2];  /* 0: left, 1: right */
};


struct cb_bst_iter
{
    struct
    {
        cb_offset_t         offset;
        struct cb_bst_node *node;
        int                 cmp;
    }           finger[64];
    uint8_t     depth;
};


enum
{
    CB_BST_BLACK = 0,
    CB_BST_RED = 1
};


static bool
cb_bst_validate(const struct cb *cb,
                cb_offset_t      node_offset,
                const char      *name);

static int
cb_map_traverse_internal(const struct cb        *cb,
                         cb_offset_t             root_node_offset,
                         cb_map_traverse_func_t  func,
                         void                   *closure);


static int
cb_key_cmp(const struct cb_key *lhs, const struct cb_key *rhs)
{
    if (lhs->k < rhs->k) return -1;
    if (lhs->k > rhs->k) return 1;
    return 0;
}


static void
cb_key_assign(struct cb_key *lhs, const struct cb_key *rhs)
{
    lhs->k = rhs->k;
}


static void
cb_value_assign(struct cb_value *lhs, const struct cb_value *rhs)
{
    lhs->v = rhs->v;
}


struct cb_command_any
{
    enum cb_command_type type;
    cb_offset_t          prev;
    union
    {
        struct cb_command_keyval    keyval;
        struct cb_command_deletekey deletekey;
        struct cb_command_bst       bst;
    };
};


static int
cb_command_alloc(struct cb             **cb,
                 cb_offset_t            *command_offset,
                 struct cb_command_any **command)
{
    cb_offset_t new_command_offset;
    int ret;

    ret = cb_memalign(cb,
                      &new_command_offset,
                      cb_alignof(struct cb_command_any),
                      sizeof(struct cb_command_any));
    if (ret != 0)
        return ret;

    *command_offset = new_command_offset;
    *command        = cb_at(*cb, new_command_offset);

    return 0;
}


static int
cb_start_data_set(struct cb_map *cb_map)
{
    cb_offset_t command_offset;
    struct cb_command_any *command;
    int ret;

    ret = cb_command_alloc(cb_map->cb, &command_offset, &command);
    if (ret != 0)
        return ret;

    command->type = CB_CMD_START_DATA;

    command->prev = cb_map->last_command_offset;
    cb_map->last_command_offset = command_offset;

    return 0;
}


static int
cb_bst_node_alloc(struct cb   **cb,
                  cb_offset_t  *node_offset)
{
    cb_offset_t new_node_offset;
    int ret;

    ret = cb_memalign(cb,
                      &new_node_offset,
                      cb_alignof(struct cb_bst_node),
                      sizeof(struct cb_bst_node));
    if (ret != 0)
        return ret;

    *node_offset = new_node_offset;

    return 0;
}


CB_INLINE struct cb_bst_node*
cb_bst_node_at(const struct cb *cb,
               cb_offset_t      node_offset)
{
    if (node_offset == CB_BST_SENTINEL)
        return NULL;

    return (struct cb_bst_node*)cb_at(cb, node_offset);
}


/*
 *  Returns 0 if found or -1 if not found.
 *  If found, iter->finger[iter->depth] will point to the node containing key.
 *  If not found, iter->finger[iter->depth] will point to the parent node for
 *     which a node containing key may be inserted.
 */
static int
cb_bst_find_path(struct cb_bst_iter  *iter,
                 const struct cb     *cb,
                 cb_offset_t          root_node_offset,
                 const struct cb_key *key)
{
    cb_offset_t         curr_offset;
    struct cb_bst_node *curr_node;
    int                 cmp;

    iter->depth = 0;

    curr_offset = root_node_offset;

    while ((curr_node = cb_bst_node_at(cb, curr_offset)) != NULL)
    {

        cmp = cb_key_cmp(key, &(curr_node->key));

        iter->finger[iter->depth].offset = curr_offset;
        iter->finger[iter->depth].node   = curr_node;
        iter->finger[iter->depth].cmp    = cmp;

        if (cmp == 0)
            return 0; /* FOUND */

        cb_assert(cmp == -1 || cmp == 1);
        curr_offset = (cmp == -1 ? curr_node->child[0] : curr_node->child[1]);
        iter->depth++;
    }

    return -1; /* NOT FOUND */
}


static bool
cb_bst_contains_key(const struct cb     *cb,
                    cb_offset_t          root_node_offset,
                    const struct cb_key *key)
{
    struct cb_bst_iter iter;
    int ret;

    ret = cb_bst_find_path(&iter,
                           cb,
                           root_node_offset,
                           key);
    return (ret == 0);
}


int
cb_bst_lookup(const struct cb     *cb,
              cb_offset_t          root_node_offset,
              const struct cb_key *key,
              struct cb_value     *value)
{
    struct cb_bst_iter iter;
    int ret;

    cb_heavy_assert(cb_bst_validate(cb, root_node_offset, "pre-lookup"));

    memset(&iter, 0, sizeof(iter)); //FIXME remove

    ret = cb_bst_find_path(&iter,
                           cb,
                           root_node_offset,
                           key);
    if (ret != 0)
        goto fail;
    cb_assert(ret == 0);

    cb_value_assign(value, &(iter.finger[iter.depth].node->value));

    cb_heavy_assert(cb_bst_validate(cb, root_node_offset, "post-lookup-success"));
    return 0;

fail:
    cb_heavy_assert(cb_bst_validate(cb, root_node_offset, "post-lookup-fail"));
    return ret;
}


CB_INLINE bool
cb_bst_node_is_modifiable(cb_offset_t node_offset,
                          cb_offset_t cutoff_offset)
{
    int cmp = cb_offset_cmp(node_offset, cutoff_offset);
    cb_assert(cmp == -1 || cmp == 0 || cmp == 1);
    return cmp > -1;
}


static int
cb_bst_select_modifiable_node(struct cb          **cb,
                              cb_offset_t          cutoff_offset,
                              cb_offset_t         *node_offset)
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

    ret = cb_bst_node_alloc(cb, &new_node_offset);
    if (ret != 0)
        return ret;

    old_node = cb_bst_node_at(*cb, old_node_offset);
    new_node = cb_bst_node_at(*cb, new_node_offset);
    memcpy(new_node, old_node, sizeof(*new_node));
#if 0
    //FIXME remove
    cb_key_assign(&(new_node->key), &(old_node->key));
    cb_value_assign(&(new_node->value), &(old_node->value));
    new_node->color    = old_node->color;
    new_node->child[0] = old_node->child[0];
    new_node->child[1] = old_node->child[1];
#endif

    *node_offset = new_node_offset;

    return 0;
}


/* direction: 0="left", 1="right" */
static int
cb_bst_rotate(struct cb   **cb,
              cb_offset_t   cutoff_offset,
              cb_offset_t  *target_node_offset,
              int           direction)
{
    cb_offset_t         demoted_node_offset;
    struct cb_bst_node *demoted_node;
    cb_offset_t         promoted_child_offset;
    struct cb_bst_node *promoted_child_node;
    int ret;

    cb_assert(direction == 0 || direction == 1);

    demoted_node_offset = *target_node_offset;
    demoted_node        = cb_bst_node_at(*cb, demoted_node_offset);

    cb_log_debug("rotating %s at node offset %ju { k: %ju, v: %ju, color: %s, left: %ju, right: %ju }",
                 direction == 0 ? "LEFT" : "RIGHT",
                 demoted_node_offset,
                 (uintmax_t)demoted_node->key.k,
                 (uintmax_t)demoted_node->value.v,
                 demoted_node->color == CB_BST_RED ? "RED" : "BLACK",
                 (uintmax_t)demoted_node->child[0],
                 (uintmax_t)demoted_node->child[1]);

    promoted_child_offset = demoted_node->child[!direction];
    cb_assert(promoted_child_offset != CB_BST_SENTINEL);

    ret = cb_bst_select_modifiable_node(cb,
                                        cutoff_offset,
                                        &demoted_node_offset);
    if (ret != 0)
        return ret;

    ret = cb_bst_select_modifiable_node(cb,
                                        cutoff_offset,
                                        &promoted_child_offset);
    if (ret != 0)
        return ret;

    /* Selecting modifiable nodes may have updated cb, so resample our
       pointers. */
    demoted_node        = cb_bst_node_at(*cb, demoted_node_offset);
    promoted_child_node = cb_bst_node_at(*cb, promoted_child_offset);

    demoted_node->child[!direction] = promoted_child_node->child[direction];
    promoted_child_node->child[direction] = demoted_node_offset;

    *target_node_offset = promoted_child_offset;

    return 0;
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


static bool
cb_bst_validate_internal(const struct cb *cb,
                         cb_offset_t      node_offset,
                         uint32_t        *tree_height,
                         uint32_t         validate_depth,
                         bool             do_print)
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

    node = cb_bst_node_at(cb, node_offset);
    if (do_print)
        printf("%.*s%snode_offset %ju: {k: %ju, v: %ju, color: %s, left: %ju, right: %ju}%s\n",
               (int)validate_depth, spaces,
               node->color == CB_BST_RED ? "\033[1;31;40m" : "",
               node_offset,
               (uintmax_t)node->key.k,
               (uintmax_t)node->value.v,
               node->color == CB_BST_RED ? "RED" : "BLACK",
               (uintmax_t)node->child[0],
               (uintmax_t)node->child[1],
               node->color == CB_BST_RED ? "\033[0m" : "");

    left_node = cb_bst_node_at(cb, node->child[0]);
    if (left_node)
    {
        if (cb_key_cmp(&(left_node->key), &(node->key)) != -1)
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju: left key %ju (off: %ju) !< key %ju\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset,
                       left_node->key.k, node->child[0],
                       node->key.k);
            retval = false;
        }
    }

    right_node = cb_bst_node_at(cb, node->child[1]);
    if (right_node)
    {
        if (cb_key_cmp(&(node->key), &(right_node->key)) != -1)
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju: key %ju !< right key %ju (off:%ju)\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset,
                       node->key.k,
                       right_node->key.k, node->child[1]);
            retval = false;
        }
    }

    if (!cb_bst_validate_internal(cb, node->child[0], &left_height, validate_depth+1, do_print))
        retval = false;

    if (!cb_bst_validate_internal(cb, node->child[1], &right_height, validate_depth+1, do_print))
        retval = false;

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
        if (cb_bst_node_is_red(cb, node->child[0]))
        {
            if (do_print)
                printf("%*.s\033[1;33;40mnode_offset %ju (red) has red left child %ju\033[0m\n",
                       (int)validate_depth, spaces,
                       node_offset, node->child[0]);
            retval = false;
        }

        if (cb_bst_node_is_red(cb, node->child[1]))
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


struct sequence_check_state
{
    bool has_prev;
    unsigned int i;
    struct cb_key prev;
    bool failed;
    bool do_print;
};


static int
sequence_check(const struct cb_key   *k,
               const struct cb_value *v,
               void                  *closure)
{
    struct sequence_check_state *scs = closure;

    (void)v;

    if (scs->do_print)
        cb_log_debug("bst[%u] = %ju.", scs->i, (uintmax_t)k->k);

    if (scs->has_prev && cb_key_cmp(&(scs->prev), k) != -1)
    {
        if (scs->do_print)
            cb_log_debug("Order violation: %ju !< %ju",
                         (uintmax_t)scs->prev.k, (uintmax_t)k->k);
        scs->failed = true;
    }

    scs->has_prev = true;
    (scs->i)++;
    cb_key_assign(&(scs->prev), k);
    return 0;
}


static bool
cb_bst_validate_sequence(const struct cb *cb,
                         cb_offset_t      node_offset,
                         bool             do_print)
{
    struct sequence_check_state scs = {
        .has_prev = false,
        .i = 0,
        .failed = false,
        .do_print = do_print
    };
    int ret;

    (void)ret;

    ret = cb_map_traverse_internal(cb, node_offset, sequence_check, &scs);
    cb_assert(ret == 0);

    return scs.failed ? false : true;
}


static bool
cb_bst_validate(const struct cb *cb,
                cb_offset_t      node_offset,
                const char      *name)
{
    uint32_t tree_height;
    bool     sequence_ok,
             structure_ok;

    /* First, just validate without printing. */
    sequence_ok  = cb_bst_validate_sequence(cb, node_offset, false);
    structure_ok = cb_bst_validate_internal(cb, node_offset, &tree_height, 0, false);
    if (sequence_ok && structure_ok)
        return true;

    /* If that failed, go through again with printing of problems. */
    if (!sequence_ok)
    {
        cb_log_error("BEGIN ERROR PRINT OF SEQUENCE %s",
                     name == NULL ? "" : name);
        cb_bst_validate_sequence(cb, node_offset, true);
        cb_log_error("END   ERROR PRINT OF SEQUENCE %s",
                     name == NULL ? "" : name);
    }

    if (!structure_ok)
    {
        cb_log_error("BEGIN ERROR PRINT OF STRUCTURE %s",
                     name == NULL ? "" : name);
        cb_bst_validate_internal(cb, node_offset, &tree_height, 0, true);
        cb_log_error("END   ERROR PRINT OF STRUCTURE %s",
                     name == NULL ? "" : name);
    }

    return false;
}


static void
cb_bst_print(const struct cb *cb,
             cb_offset_t      node_offset)
{
    uint32_t tree_height;

    if (cb_bst_validate(cb, node_offset, NULL))
    {
        /* If we validated, then just print. */
        cb_bst_validate_internal(cb, node_offset, &tree_height, 0, true);
    }
    else
    {
        /* Assume the failure has already printed */
        cb_log_debug("BOGUS TREE");
    }
}


struct cb_bst_insert_state
{
    cb_offset_t greatgrandparent_node_offset;
    cb_offset_t grandparent_node_offset;
    cb_offset_t parent_node_offset;
    cb_offset_t curr_node_offset;
    cb_offset_t new_root_node_offset;
    cb_offset_t cutoff_offset;
    int         greatgrandparent_to_grandparent_dir;
    int         grandparent_to_parent_dir;
    int         parent_to_curr_dir;
    int         dir;
};


struct cb_bst_delete_state
{
    cb_offset_t grandparent_node_offset;
    cb_offset_t parent_node_offset;
    cb_offset_t curr_node_offset;
    cb_offset_t sibling_node_offset;
    cb_offset_t new_root_node_offset;
    cb_offset_t cutoff_offset;
    int         grandparent_to_parent_dir;
    int         parent_to_curr_dir;
    int         dir;
};


static bool cb_bst_insert_state_validate(struct cb                  *cb,
                                         struct cb_bst_insert_state *s)
{
    bool is_ok = true;

    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] != s->grandparent_node_offset)
        {
            is_ok = false;
            cb_log_debug("greatgrandparent doesn't point to grandparent");
        }
    }

    if (s->grandparent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->grandparent_node_offset)->child[
               s->grandparent_to_parent_dir] != s->parent_node_offset)
        {
            is_ok = false;
            cb_log_debug("grandparent doesn't point to parent");
        }
    }

    if (s->parent_node_offset != CB_BST_SENTINEL)
    {
        if (cb_bst_node_at(cb, s->parent_node_offset)->child[
               s->parent_to_curr_dir] != s->curr_node_offset)
        {
            is_ok = false;
            cb_log_debug("parent doesn't point to current");
        }
    }

    if (!is_ok)
    {
        cb_log_debug("greatgrandparent_node_offset: %ju",
                      s->greatgrandparent_node_offset);
        cb_log_debug("grandparent_node_offset: %ju",
                     s->grandparent_node_offset);
        cb_log_debug("parent_node_offset: %ju", s->parent_node_offset);
        cb_log_debug("curr_node_offset: %ju", s->curr_node_offset);
        cb_log_debug("new_root_node_offset: %ju", s->new_root_node_offset);
        cb_log_debug("greatgrandparent_to_grandparent_dir: %d",
                     s->greatgrandparent_to_grandparent_dir);
        cb_log_debug("grandparent_to_parent_dir: %d",
                     s->grandparent_to_parent_dir);
        cb_log_debug("parent_to_curr_dir: %d", s->parent_to_curr_dir);
        cb_log_debug("dir: %d", s->dir);
    }

    return is_ok;
}


static void
cb_bst_print_insert0(const struct cb            *cb,
                     cb_offset_t                 node_offset,
                     uint32_t                    validate_depth,
                     struct cb_bst_insert_state *s)
{
    struct cb_bst_node *node, *left_node, *right_node;
    static char spaces[] = "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t";

    if (node_offset == CB_BST_SENTINEL)
        return;

    node = cb_bst_node_at(cb, node_offset);
    printf("%.*s%snode_offset %ju: {k: %ju, v: %ju, color: %s, left: %ju, right: %ju}%s%s%s%s%s\n",
           (int)validate_depth, spaces,
           node->color == CB_BST_RED ? "\033[1;31;40m" : "",
           node_offset,
           (uintmax_t)node->key.k,
           (uintmax_t)node->value.v,
           node->color == CB_BST_RED ? "RED" : "BLACK",
           (uintmax_t)node->child[0],
           (uintmax_t)node->child[1],
           node->color == CB_BST_RED ? "\033[0m" : "",
           (node_offset == s->greatgrandparent_node_offset ? " GREATGRANDPARENT" : ""),
           (node_offset == s->grandparent_node_offset ? " GRANDPARENT" : ""),
           (node_offset == s->parent_node_offset ? " PARENT" : ""),
           (node_offset == s->curr_node_offset ? " CURRENT" : ""));

    left_node = cb_bst_node_at(cb, node->child[0]);
    if (left_node)
    {
        if (cb_key_cmp(&(left_node->key), &(node->key)) != -1)
        {
            printf("%*.snode_offset %ju: left key %ju (off: %ju) !< key %ju\n",
                   (int)validate_depth, spaces,
                   node_offset,
                   left_node->key.k, node->child[0],
                   node->key.k);
        }
    }

    right_node = cb_bst_node_at(cb, node->child[1]);
    if (right_node)
    {
        if (cb_key_cmp(&(node->key), &(right_node->key)) != -1)
        {
            printf("%*.snode_offset %ju: key %ju !< right key %ju (off:%ju)\n",
                   (int)validate_depth, spaces,
                   node_offset,
                   node->key.k,
                   right_node->key.k, node->child[1]);
        }
    }

    cb_bst_print_insert0(cb, node->child[0], validate_depth+1, s);
    cb_bst_print_insert0(cb, node->child[1], validate_depth+1, s);
}


static void
cb_bst_print_insert(const struct cb            *cb,
                    struct cb_bst_insert_state *s)
{
    cb_bst_print_insert0(cb, s->new_root_node_offset, 0, s);
}


static void
cb_bst_print_delete0(const struct cb            *cb,
                     cb_offset_t                 node_offset,
                     uint32_t                    validate_depth,
                     struct cb_bst_delete_state *s)
{
    struct cb_bst_node *node, *left_node, *right_node;
    static char spaces[] = "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t";

    if (node_offset == CB_BST_SENTINEL)
        return;

    node = cb_bst_node_at(cb, node_offset);
    printf("%.*s%snode_offset %ju: {k: %ju, v: %ju, color: %s, left: %ju, right: %ju}%s%s%s%s\n",
           (int)validate_depth, spaces,
           node->color == CB_BST_RED ? "\033[1;31;40m" : "",
           node_offset,
           (uintmax_t)node->key.k,
           (uintmax_t)node->value.v,
           node->color == CB_BST_RED ? "RED" : "BLACK",
           (uintmax_t)node->child[0],
           (uintmax_t)node->child[1],
           node->color == CB_BST_RED ? "\033[0m" : "",
           (node_offset == s->grandparent_node_offset ? " GRANDPARENT" : ""),
           (node_offset == s->parent_node_offset ? " PARENT" : ""),
           (node_offset == s->curr_node_offset ? " CURRENT" : ""));

    left_node = cb_bst_node_at(cb, node->child[0]);
    if (left_node)
    {
        if (cb_key_cmp(&(left_node->key), &(node->key)) != -1)
        {
            printf("%*.snode_offset %ju: left key %ju (off: %ju) !< key %ju\n",
                   (int)validate_depth, spaces,
                   node_offset,
                   left_node->key.k, node->child[0],
                   node->key.k);
        }
    }

    right_node = cb_bst_node_at(cb, node->child[1]);
    if (right_node)
    {
        if (cb_key_cmp(&(node->key), &(right_node->key)) != -1)
        {
            printf("%*.snode_offset %ju: key %ju !< right key %ju (off:%ju)\n",
                   (int)validate_depth, spaces,
                   node_offset,
                   node->key.k,
                   right_node->key.k, node->child[1]);
        }
    }

    cb_bst_print_delete0(cb, node->child[0], validate_depth+1, s);
    cb_bst_print_delete0(cb, node->child[1], validate_depth+1, s);
}


static void
cb_bst_print_delete(const struct cb            *cb,
                    struct cb_bst_delete_state *s)
{
    cb_bst_print_delete0(cb, s->new_root_node_offset, 0, s);
}


static bool
cb_bst_delete_state_validate(struct cb                  *cb,
                             struct cb_bst_delete_state *s)
{
    (void)cb, (void)s;

    cb_assert(s->grandparent_node_offset == CB_BST_SENTINEL ||
           cb_bst_node_at(cb, s->grandparent_node_offset)->child[
               s->grandparent_to_parent_dir] == s->parent_node_offset);
    cb_assert(s->parent_node_offset == CB_BST_SENTINEL ||
           cb_bst_node_at(cb, s->parent_node_offset)->child[
               s->parent_to_curr_dir] == s->curr_node_offset);
    //FIXME check sibling_node_offset?
    return true;
}


static const struct cb_bst_insert_state CB_BST_INSERT_STATE_INIT =
    {
        .greatgrandparent_node_offset        = CB_BST_SENTINEL,
        .grandparent_node_offset             = CB_BST_SENTINEL,
        .parent_node_offset                  = CB_BST_SENTINEL,
        .curr_node_offset                    = CB_BST_SENTINEL,
        .new_root_node_offset                = CB_BST_SENTINEL,
        .cutoff_offset                       = CB_BST_SENTINEL,
        .greatgrandparent_to_grandparent_dir = 1,
        .grandparent_to_parent_dir           = 1,
        .parent_to_curr_dir                  = 1
    };


static const struct cb_bst_delete_state CB_BST_DELETE_STATE_INIT =
    {
        .grandparent_node_offset             = CB_BST_SENTINEL,
        .parent_node_offset                  = CB_BST_SENTINEL,
        .curr_node_offset                    = CB_BST_SENTINEL,
        .sibling_node_offset                 = CB_BST_SENTINEL, /*FIXME can remove?*/
        .new_root_node_offset                = CB_BST_SENTINEL,
        .cutoff_offset                       = CB_BST_SENTINEL,
        .grandparent_to_parent_dir           = 1,
        .parent_to_curr_dir                  = 1
    };


static int
cb_bst_red_pair_fixup_single(struct cb                  **cb,
                             struct cb_bst_insert_state  *s)
{
    /*
      grandparent 3,B       parent 2,B
                  / \              / \
         parent 2,R  d      curr 1,R 3,R
                / \              / \ / \
         curr 1,R  c            a  b c  d
              / \
             a   b
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                node1_offset,
                old_node2_offset,
                old_node3_offset,
                new_node2_offset,
                new_node3_offset;

    struct cb_bst_node *old_node2,
                       *old_node3,
                       *new_node2,
                       *new_node3;
    int ret;

    cb_log_debug("DANDEBUG fixup_single @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check preconditions */
    cb_assert(s->grandparent_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(s->grandparent_to_parent_dir == s->parent_to_curr_dir);
    // FIXME more checks

    node1_offset     = s->curr_node_offset;
    old_node2_offset = s->parent_node_offset;
    old_node3_offset = s->grandparent_node_offset;

    cb_assert(cb_bst_node_is_red(*cb, node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node2_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node3_offset));

    c_node_offset = cb_bst_node_at(*cb, old_node2_offset)->child[!s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[!s->grandparent_to_parent_dir];


    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node2_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;

    old_node2 = cb_bst_node_at(*cb, old_node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    new_node2 = cb_bst_node_at(*cb, new_node2_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    cb_key_assign(&(new_node2->key), &(old_node2->key));
    cb_value_assign(&(new_node2->value), &(old_node2->value));
    new_node2->color = CB_BST_BLACK;
    new_node2->child[s->parent_to_curr_dir] = node1_offset;
    new_node2->child[!s->parent_to_curr_dir] = new_node3_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->parent_to_curr_dir] = c_node_offset;
    new_node3->child[!s->parent_to_curr_dir] = d_node_offset;

    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        cb_bst_node_at(*cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] = new_node2_offset;
    }

    if (s->new_root_node_offset == old_node3_offset)
        s->new_root_node_offset = new_node2_offset;

    s->grandparent_node_offset             = s->greatgrandparent_node_offset;
    s->grandparent_to_parent_dir           = s->greatgrandparent_to_grandparent_dir;
    s->parent_node_offset                  = new_node2_offset;
    s->greatgrandparent_node_offset        = CB_BST_SENTINEL;
    s->greatgrandparent_to_grandparent_dir = -1;

    cb_assert(cb_bst_insert_state_validate(*cb, s));

    return 0;
}


static int
cb_bst_red_pair_fixup_double(struct cb                  **cb,
                             struct cb_bst_insert_state  *s)
{
    /*
      grandparent 3,B       parent 2,B
                  / \              / \
         parent 1,R  d           1,R 3,R
                / \              / \ / \   NOTE: curr is 1 or 3, depending
               a  2,R curr      a  b c  d        on dir.
                  / \
                 b   c
     */

    cb_offset_t a_node_offset,
                b_node_offset,
                c_node_offset,
                d_node_offset,
                old_node1_offset,
                old_node2_offset,
                old_node3_offset,
                new_node1_offset,
                new_node2_offset,
                new_node3_offset;

    struct cb_bst_node *old_node1,
                       *old_node2,
                       *old_node3,
                       *new_node1,
                       *new_node2,
                       *new_node3;
    int ret;

    cb_log_debug("DANDEBUG fixup_double @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check preconditions */
    cb_assert(s->grandparent_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(s->grandparent_to_parent_dir != s->parent_to_curr_dir);
    //FIXME more checks

    old_node1_offset = s->parent_node_offset;
    old_node2_offset = s->curr_node_offset;
    old_node3_offset = s->grandparent_node_offset;

    cb_assert(cb_bst_node_is_red(*cb, old_node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node2_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node3_offset));

    a_node_offset = cb_bst_node_at(*cb, old_node1_offset)->child[!s->parent_to_curr_dir];
    b_node_offset = cb_bst_node_at(*cb, old_node2_offset)->child[!s->parent_to_curr_dir];
    c_node_offset = cb_bst_node_at(*cb, old_node2_offset)->child[s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[!s->grandparent_to_parent_dir];

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node2_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node1_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;

    old_node1 = cb_bst_node_at(*cb, old_node1_offset);
    old_node2 = cb_bst_node_at(*cb, old_node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);

    new_node1 = cb_bst_node_at(*cb, new_node1_offset);
    new_node2 = cb_bst_node_at(*cb, new_node2_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    cb_key_assign(&(new_node1->key), &(old_node1->key));
    cb_value_assign(&(new_node1->value), &(old_node1->value));
    new_node1->color = CB_BST_RED;
    new_node1->child[!s->parent_to_curr_dir] = a_node_offset;
    new_node1->child[s->parent_to_curr_dir] = b_node_offset;

    cb_key_assign(&(new_node2->key), &(old_node2->key));
    cb_value_assign(&(new_node2->value), &(old_node2->value));
    new_node2->color = CB_BST_BLACK;
    new_node2->child[s->grandparent_to_parent_dir] = new_node1_offset;
    new_node2->child[!s->grandparent_to_parent_dir] = new_node3_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->grandparent_to_parent_dir] = c_node_offset;
    new_node3->child[!s->grandparent_to_parent_dir] = d_node_offset;

    if (s->greatgrandparent_node_offset != CB_BST_SENTINEL)
    {
        cb_bst_node_at(*cb, s->greatgrandparent_node_offset)->child[
            s->greatgrandparent_to_grandparent_dir] = new_node2_offset;
    }

    if (s->new_root_node_offset == old_node3_offset)
        s->new_root_node_offset = new_node2_offset;

    /* Maintain iterator */
    s->grandparent_node_offset      = s->greatgrandparent_node_offset;
    s->grandparent_to_parent_dir    = s->greatgrandparent_to_grandparent_dir;
    s->parent_node_offset           = new_node2_offset;
    if (s->dir == s->parent_to_curr_dir)
    {
        s->curr_node_offset = new_node3_offset;
        s->dir              = !s->parent_to_curr_dir;
        /* s->parent_to_curr_dir remains same. */
    }
    else
    {
        s->curr_node_offset   = new_node1_offset;
        s->dir                = s->parent_to_curr_dir;
        s->parent_to_curr_dir = !s->parent_to_curr_dir;
    }
    s->greatgrandparent_node_offset        = CB_BST_SENTINEL;
    s->greatgrandparent_to_grandparent_dir = -1;

    cb_assert(cb_bst_insert_state_validate(*cb, s));

    return 0;
}


/* NOTE: Insertion uses a top-down method. */
int
cb_bst_insert(struct cb             **cb,
              cb_offset_t            *root_node_offset,
              cb_offset_t             cutoff_offset,
              const struct cb_key    *key,
              const struct cb_value  *value)
{
    struct cb_bst_insert_state s = CB_BST_INSERT_STATE_INIT;
    cb_offset_t         initial_cursor_offset = cb_cursor(*cb),
                        left_child_offset,
                        right_child_offset;
    struct cb_bst_node *parent_node,
                       *curr_node,
                       *left_child_node,
                       *right_child_node,
                       *root_node;
    int                 cmp;
    int ret;

    cb_log_debug("DANDEBUG insert of key %ju, value %ju", key->k, value->v);

    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "pre-insert"));

    s.new_root_node_offset = *root_node_offset;
    s.curr_node_offset = s.new_root_node_offset;
    s.cutoff_offset    = cutoff_offset;

    if (s.curr_node_offset == CB_BST_SENTINEL)
    {
        /* The tree is empty, insert a new black node. */
        ret = cb_bst_node_alloc(cb, &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        curr_node->color    = CB_BST_BLACK;
        curr_node->child[0] = CB_BST_SENTINEL;
        curr_node->child[1] = CB_BST_SENTINEL;
        cb_key_assign(&(curr_node->key), key);
        cb_value_assign(&(curr_node->value), value);

        *root_node_offset = s.curr_node_offset;

        cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "post-insert-success0"));

        return 0;
    }

    ret = cb_bst_select_modifiable_node(cb,
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
                                            cutoff_offset,
                                            &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
        parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;

entry:
        cb_assert(cb_bst_node_is_modifiable(s.curr_node_offset, cutoff_offset));
        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        cmp = cb_key_cmp(key, &(curr_node->key));
        if (cmp == 0)
        {
            /* The key already exists in this tree.  Update the value at key
               and go no further. */
            cb_value_assign(&(curr_node->value), value);
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
                                                cutoff_offset,
                                                &left_child_offset);
            if (ret != 0)
                goto fail;

            ret = cb_bst_select_modifiable_node(cb,
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
                    ret = cb_bst_red_pair_fixup_single(cb, &s);
                else
                    ret = cb_bst_red_pair_fixup_double(cb, &s);

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

    ret = cb_bst_node_alloc(cb, &s.curr_node_offset);
    if (ret != 0)
        goto fail;

    /* Allocating a node may have updated cb, so resample our pointers. */

    parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
    parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;

    curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
    curr_node->color    = CB_BST_RED;
    curr_node->child[0] = CB_BST_SENTINEL;
    curr_node->child[1] = CB_BST_SENTINEL;
    cb_key_assign(&(curr_node->key), key);
    cb_value_assign(&(curr_node->value), value);

    if (cb_bst_node_is_red(*cb, s.parent_node_offset))
    {
        if (s.grandparent_to_parent_dir == s.parent_to_curr_dir)
            ret = cb_bst_red_pair_fixup_single(cb, &s);
        else
            ret = cb_bst_red_pair_fixup_double(cb, &s);

        if (ret != 0)
            goto fail;
    }

done:
    root_node = cb_bst_node_at(*cb, s.new_root_node_offset);
    root_node->color = CB_BST_BLACK;

    *root_node_offset = s.new_root_node_offset;

    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "post-insert-success"));

    return 0;

fail:
    cb_rewind_to(*cb, initial_cursor_offset);
    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "post-insert-fail"));
    return ret;
}


static int
cb_bst_delete_fix_root(struct cb                  **cb,
                       struct cb_bst_delete_state  *s)
{

    /*
            curr 2,R             parent 3,R
            dir /   \ !dir             /   \
              1,B   3,R         curr 2,R    d
              / \   / \              / \
             a   b c   d           1,B  c
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

    cb_log_debug("DANDEBUG case fixroot @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
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
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;

    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    cb_assert(cb_bst_node_is_modifiable(node2_offset, s->cutoff_offset));
    cb_assert(node2->child[s->dir] == node1_offset);
    node2->child[!s->dir] = c_node_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
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
    cb_assert(cb_bst_delete_state_validate(*cb, s));
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
                    struct cb_bst_delete_state  *s)
{
    /*
         parent 4,R                    4,R
                / \                    / \
         curr 2,B  e          parent 3,B  e
              / \                    / \
        dir  /   \  !dir      curr 2,R  d
           1,B   3,R          dir  / \ !dir
           / \   / \             1,B  c
          a   b c   d            / \
                                a   b
     */

    cb_offset_t c_node_offset,
                d_node_offset,
                e_node_offset,
                node1_offset, /* unchanged */
                node2_offset, /* modifiable FIXME assert */
                node4_offset, /* modifiable FIXME assert */
                old_node3_offset,
                new_node3_offset;

    struct cb_bst_node *node2,
                       *node4,
                       *old_node3,
                       *new_node3;

    int ret;

    cb_log_debug("DANDEBUG case1 @ curr_node_offset: %ju", (uintmax_t)s->curr_node_offset);
#if 0
    cb_log_debug("DANDEBUG case1 BEGIN TREE0", (uintmax_t)s->curr_node_offset);
    cb_bst_print_delete(*cb, s);
    cb_log_debug("DANDEBUG case1 END TREE0", (uintmax_t)s->curr_node_offset);
#endif

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir])->child[1]));


    node1_offset     = cb_bst_node_at(*cb, s->curr_node_offset)->child[s->dir];
    node2_offset     = s->curr_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->curr_node_offset)->child[!s->dir];
    node4_offset     = s->parent_node_offset;

    cb_assert(cb_bst_node_is_black(*cb, node1_offset));
    cb_assert(cb_bst_node_is_black(*cb, node2_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node3_offset));
    cb_assert(cb_bst_node_is_red(*cb, node4_offset));

    c_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[s->dir];
    d_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[!s->dir];
    e_node_offset = cb_bst_node_at(*cb, node4_offset)->child[!s->parent_to_curr_dir];

#if 0
    cb_log_debug("DANDEBUG case1 node1: %ju", (uintmax_t)node1_offset);
    cb_log_debug("DANDEBUG case1 old_node2: %ju", (uintmax_t)old_node2_offset);
    cb_log_debug("DANDEBUG case1 old_node3: %ju", (uintmax_t)old_node3_offset);
    cb_log_debug("DANDEBUG case1 old_node4: %ju", (uintmax_t)old_node4_offset);
    cb_log_debug("DANDEBUG case1 c: %ju", (uintmax_t)c_node_offset);
    cb_log_debug("DANDEBUG case1 d: %ju", (uintmax_t)d_node_offset);
    cb_log_debug("DANDEBUG case1 e: %ju", (uintmax_t)e_node_offset);
#endif

    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;

    node2 = cb_bst_node_at(*cb, node2_offset);
    node4 = cb_bst_node_at(*cb, node4_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);

    cb_assert(cb_bst_node_is_modifiable(node2_offset, s->cutoff_offset));
    node2->color = CB_BST_RED;
    node2->child[s->dir] = node1_offset;
    node2->child[!s->dir] = c_node_offset;

    new_node3->color = CB_BST_BLACK;
    new_node3->child[s->dir] = node2_offset;
    new_node3->child[!s->dir] = d_node_offset;
    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));

    cb_assert(cb_bst_node_is_modifiable(node4_offset, s->cutoff_offset));
    node4->color = CB_BST_RED;
    node4->child[s->parent_to_curr_dir] = new_node3_offset;
    node4->child[!s->parent_to_curr_dir] = e_node_offset;

    /* PREPARE FOR DESCEND */
    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = node4_offset;

    s->grandparent_node_offset   = node4_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->parent_node_offset        = new_node3_offset;
    s->parent_to_curr_dir        = s->dir;
    s->curr_node_offset          = node2_offset;
    s->sibling_node_offset       = cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir];

    /* Check post-conditions */
#if 0
    cb_log_debug("DANDEBUG case1 BEGIN TREE1", (uintmax_t)s->curr_node_offset);
    cb_bst_print_delete(*cb, s);
    cb_log_debug("DANDEBUG case1 END TREE1", (uintmax_t)s->curr_node_offset);
#endif

    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[!s->grandparent_to_parent_dir]));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case2(struct cb                  **cb,
                    struct cb_bst_delete_state  *s)
{

    /*
      parent      2,R                      3,R
                 /   \                    /   \
                /     \                  /     \
      curr    1,B     4,B      parent  2,B     4,B
              / \     / \              / \     / \
             a   b  3,R  e     curr  1,R  c   d   e
                    / \              / \
                   c   d            a   b
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

    cb_log_debug("DANDEBUG case2 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(s->sibling_node_offset ==
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir])->child[1]));
    cb_assert(s->curr_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[s->parent_to_curr_dir]);
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);

    node1_offset     = s->curr_node_offset;
    node2_offset     = s->parent_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir];
    old_node4_offset = s->sibling_node_offset;

    cb_assert(cb_bst_node_is_black(*cb, node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, node2_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node3_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node4_offset));

    c_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[!s->parent_to_curr_dir];
    e_node_offset = cb_bst_node_at(*cb, old_node4_offset)->child[!s->parent_to_curr_dir];

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node4_offset);
    if (ret != 0)
        return ret;

    cb_log_debug("DANDEBUG offsets: node1 %ju, node2 %ju, node3 %ju, node4 %ju",
                 node1_offset, node2_offset, new_node3_offset, new_node4_offset);

    node1     = cb_bst_node_at(*cb, node1_offset);
    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    old_node4 = cb_bst_node_at(*cb, old_node4_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);
    new_node4 = cb_bst_node_at(*cb, new_node4_offset);


    cb_assert(cb_bst_node_is_modifiable(node1_offset, s->cutoff_offset));
    node1->color = CB_BST_RED;

    cb_assert(cb_bst_node_is_modifiable(node2_offset, s->cutoff_offset));
    node2->color = CB_BST_BLACK;
    node2->child[!s->parent_to_curr_dir] = c_node_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->parent_to_curr_dir] = node2_offset;
    new_node3->child[!s->parent_to_curr_dir] = new_node4_offset;

    cb_key_assign(&(new_node4->key), &(old_node4->key));
    cb_value_assign(&(new_node4->value), &(old_node4->value));
    new_node4->color = CB_BST_BLACK;
    new_node4->child[s->parent_to_curr_dir] = d_node_offset;
    new_node4->child[!s->parent_to_curr_dir] = e_node_offset;

    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = new_node3_offset;

    if (s->new_root_node_offset == node2_offset)
        s->new_root_node_offset = new_node3_offset;

    s->grandparent_node_offset   = new_node3_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->parent_node_offset        = node2_offset; //FIXME Redundant, assert
    s->curr_node_offset          = node1_offset; //FIXME Redundant, assert
    s->sibling_node_offset       = c_node_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


#if 0
static int cb_bst_delete_case3(struct cb                  **cb,
                               struct cb_bst_delete_state  *s,
                               const struct cb_key         *key)
{

    /*
      parent      2,R                       4,R
                 /   \                     /   \
                /     \                   /     \
      curr    1,B     4,R        parent 2,R     5,B
              / \     / \               / \     / \
             a   b 3,B   5,B    curr 1,B   3,B e   f
                   / \   / \         / \   / \
                  c   d e   f       a   b c   d
     */

    cb_offset_t node5_offset,
                old_node1_offset,
                old_node2_offset,
                old_node3_offset,
                old_node4_offset,
                new_node1_offset,
                new_node2_offset,
                new_node3_offset,
                new_node4_offset;

    struct cb_bst_node *old_node1,
                       *old_node2,
                       *old_node3,
                       *old_node4,
                       *new_node1,
                       *new_node2,
                       *new_node3,
                       *new_node4;

    int ret;

    cb_log_debug("DANDEBUG case3");

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[1]));
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(s->sibling_node_offset ==
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[1]));

    old_node1_offset = s->curr_node_offset;
    old_node2_offset = s->parent_node_offset;
    old_node3_offset = cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir];
    old_node4_offset = s->sibling_node_offset;
    node5_offset     = cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir];

    cb_assert(cb_bst_node_is_black(*cb, old_node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node2_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node3_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node4_offset));
    cb_assert(cb_bst_node_is_black(*cb, node5_offset));

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node4_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node2_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node1_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;

    old_node1 = cb_bst_node_at(*cb, old_node1_offset);
    old_node2 = cb_bst_node_at(*cb, old_node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    old_node4 = cb_bst_node_at(*cb, old_node4_offset);
    new_node1 = cb_bst_node_at(*cb, new_node1_offset);
    new_node2 = cb_bst_node_at(*cb, new_node2_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);
    new_node4 = cb_bst_node_at(*cb, new_node4_offset);

    cb_key_assign(&(new_node1->key), &(old_node1->key));
    cb_value_assign(&(new_node1->value), &(old_node1->value));
    new_node1->color = CB_BST_BLACK;
    new_node1->child[0] = old_node1->child[0];
    new_node1->child[1] = old_node1->child[1];

    cb_key_assign(&(new_node2->key), &(old_node2->key));
    cb_value_assign(&(new_node2->value), &(old_node2->value));
    new_node2->color = CB_BST_BLACK;
    new_node2->child[s->parent_to_curr_dir]  = new_node1_offset;
    new_node2->child[!s->parent_to_curr_dir] = new_node3_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_BLACK;
    new_node3->child[0] = old_node3->child[0];
    new_node3->child[1] = old_node3->child[1];

    cb_key_assign(&(new_node4->key), &(old_node4->key));
    cb_value_assign(&(new_node4->value), &(old_node4->value));
    new_node4->color = CB_BST_RED;
    new_node4->child[s->parent_to_curr_dir] = new_node2_offset;
    new_node4->child[!s->parent_to_curr_dir] = node5_offset;

    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = new_node4_offset;

    if (s->new_root_node_offset == old_node2_offset)
        s->new_root_node_offset = new_node4_offset;

    s->grandparent_node_offset   = new_node4_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->parent_node_offset        = new_node2_offset;
    s->curr_node_offset          = new_node1_offset;
    s->sibling_node_offset       = new_node3_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_red(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[1]));

    return 0;
}
#endif


static int
cb_bst_delete_case4(struct cb                  **cb,
                    struct cb_bst_delete_state  *s)
{
    /*
      parent      2,R                      3,R
                 /   \                    /   \
                /     \                  /     \
      curr    1,B     4,B      parent  2,B     4,B
              / \     / \              / \     / \
             a   b  3,R  e     curr  1,R  c   d   e
                    / \              / \
                   c   d            a   b
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

    cb_log_debug("DANDEBUG case4 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(s->sibling_node_offset ==
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir])->child[1]));


    node1_offset     = s->curr_node_offset;
    node2_offset     = s->parent_node_offset;
    old_node3_offset = s->sibling_node_offset;
    old_node4_offset = cb_bst_node_at(*cb, s->sibling_node_offset)->child[!s->parent_to_curr_dir];

    cb_assert(cb_bst_node_is_black(*cb, node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, node2_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node3_offset));
    cb_assert(cb_bst_node_is_red(*cb, old_node4_offset));

    c_node_offset = cb_bst_node_at(*cb, old_node3_offset)->child[s->parent_to_curr_dir];
    d_node_offset = cb_bst_node_at(*cb, old_node4_offset)->child[s->parent_to_curr_dir];
    e_node_offset = cb_bst_node_at(*cb, old_node4_offset)->child[!s->parent_to_curr_dir];

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node3_offset);
    if (ret != 0)
        return ret;
    ret = cb_bst_node_alloc(cb, &new_node4_offset);
    if (ret != 0)
        return ret;

    cb_log_debug("DANDEBUG node1 %ju, node2 %ju, old_node3 %ju, old_node4 %ju, new_node3 %ju, new_node4: %ju",
                 node1_offset, node2_offset, old_node3_offset, old_node4_offset, new_node3_offset, new_node4_offset);

    node1     = cb_bst_node_at(*cb, node1_offset);
    node2     = cb_bst_node_at(*cb, node2_offset);
    old_node3 = cb_bst_node_at(*cb, old_node3_offset);
    old_node4 = cb_bst_node_at(*cb, old_node4_offset);
    new_node3 = cb_bst_node_at(*cb, new_node3_offset);
    new_node4 = cb_bst_node_at(*cb, new_node4_offset);

    cb_assert(cb_bst_node_is_modifiable(node1_offset, s->cutoff_offset));
    node1->color = CB_BST_RED;

    cb_assert(cb_bst_node_is_modifiable(node2_offset, s->cutoff_offset));
    node2->color = CB_BST_BLACK;
    cb_assert(node2->child[s->parent_to_curr_dir] == node1_offset);
    node2->child[!s->parent_to_curr_dir] = c_node_offset;

    cb_key_assign(&(new_node3->key), &(old_node3->key));
    cb_value_assign(&(new_node3->value), &(old_node3->value));
    new_node3->color = CB_BST_RED;
    new_node3->child[s->parent_to_curr_dir] = node2_offset;
    new_node3->child[!s->parent_to_curr_dir] = new_node4_offset;

    cb_key_assign(&(new_node4->key), &(old_node4->key));
    cb_value_assign(&(new_node4->value), &(old_node4->value));
    new_node4->color = CB_BST_BLACK;
    new_node4->child[s->parent_to_curr_dir] = d_node_offset;
    new_node4->child[!s->parent_to_curr_dir] = e_node_offset;

    if (s->grandparent_node_offset != CB_BST_SENTINEL)
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[s->grandparent_to_parent_dir] = new_node3_offset;

    if (s->new_root_node_offset == node2_offset)
        s->new_root_node_offset = new_node3_offset;

    s->grandparent_node_offset   = new_node3_offset;
    s->grandparent_to_parent_dir = s->parent_to_curr_dir;
    s->parent_node_offset        = node2_offset; //FIXME redundant, assert
    s->curr_node_offset          = node1_offset; //FIXME redundant, assert
    s->sibling_node_offset       = c_node_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_red(*cb, s->grandparent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->grandparent_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));

    return 0;
}


static int
cb_bst_delete_case5(struct cb                  **cb,
                    struct cb_bst_delete_state  *s)
{
    /*
      parent      3,R                3,B
                 /   \              /   \
                /     \            /     \
      curr    1,B     5,B        1,R     5,R
              / \     / \        / \     / \
            0,B 2,B 4,B 6,B    0,B 2,B 4,B 6,B
            / \ / \ / \ / \    / \ / \ / \ / \
            a b c d e f g h    a b c d e f g h
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

    cb_log_debug("DANDEBUG case5 @ %ju", (uintmax_t)s->curr_node_offset);

    /* Check pre-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_black(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(s->curr_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(s->sibling_node_offset ==
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(s->sibling_node_offset != CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_black(*cb, s->sibling_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->sibling_node_offset)->child[1]));

    node1_offset     = s->curr_node_offset;
    node3_offset     = s->parent_node_offset;
    old_node5_offset = s->sibling_node_offset;

    cb_assert(cb_bst_node_is_black(*cb, node1_offset));
    cb_assert(cb_bst_node_is_red(*cb, node3_offset));
    cb_assert(cb_bst_node_is_black(*cb, old_node5_offset));

    /* Allocated traversal-contiguous. */
    ret = cb_bst_node_alloc(cb, &new_node5_offset);
    if (ret != 0)
        return ret;

    node1     = cb_bst_node_at(*cb, node1_offset);
    node3     = cb_bst_node_at(*cb, node3_offset);
    old_node5 = cb_bst_node_at(*cb, old_node5_offset);
    new_node5 = cb_bst_node_at(*cb, new_node5_offset);

    cb_assert(cb_bst_node_is_modifiable(node1_offset, s->cutoff_offset));
    node1->color = CB_BST_RED;

    cb_assert(cb_bst_node_is_modifiable(node3_offset, s->cutoff_offset));
    node3->color = CB_BST_BLACK;
    cb_assert(node3->child[s->parent_to_curr_dir] == node1_offset);
    node3->child[!s->parent_to_curr_dir] = new_node5_offset;

    cb_key_assign(&(new_node5->key), &(old_node5->key));
    cb_value_assign(&(new_node5->value), &(old_node5->value));
    new_node5->color = CB_BST_RED;
    new_node5->child[0] = old_node5->child[0];
    new_node5->child[1] = old_node5->child[1];


    /* Maintain iterator. */
    s->sibling_node_offset     = new_node5_offset;

    /* Check post-conditions */
    cb_assert(cb_bst_delete_state_validate(*cb, s));
    cb_assert(s->sibling_node_offset == cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]);
    cb_assert(cb_bst_node_is_black(*cb, s->parent_node_offset));
    cb_assert(cb_bst_node_is_red(*cb, s->curr_node_offset));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, s->curr_node_offset)->child[1]));
    cb_assert(cb_bst_node_is_red(*cb,
        cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[0]));
    cb_assert(cb_bst_node_is_black(*cb,
        cb_bst_node_at(*cb, cb_bst_node_at(*cb, s->parent_node_offset)->child[!s->parent_to_curr_dir])->child[1]));

    return 0;
}


int
cb_bst_delete(struct cb             **cb,
              cb_offset_t            *root_node_offset,
              cb_offset_t             cutoff_offset,
              const struct cb_key    *key)
{
    struct cb_bst_delete_state s = CB_BST_DELETE_STATE_INIT;
    cb_offset_t         initial_cursor_offset = cb_cursor(*cb),
                        found_node_offset = CB_BST_SENTINEL;
    struct cb_bst_node *root_node,
                       *parent_node,
                       *curr_node;
    int                 cmp;
    int ret;
    //cb_offset_t old_curr_node_offset;
    //unsigned int iter = 0;

    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "pre-delete"));
#if 0
    cb_log_debug("DANDEBUG BEGIN PRE-DELETE (cutoff_offset: %ju)", cutoff_offset);
    cb_bst_print(*cb, *root_node_offset);
    cb_log_debug("DANDEBUG END PRE-DELETE");
#endif

    s.curr_node_offset = *root_node_offset;
    s.cutoff_offset    = cutoff_offset;

    /* For empty trees, there is nothing to do. */
    if (s.curr_node_offset == CB_BST_SENTINEL)
    {
        ret = -1;
        goto fail;
    }

    //old_curr_node_offset = s.curr_node_offset;
    ret = cb_bst_select_modifiable_node(cb,
                                        cutoff_offset,
                                        &s.curr_node_offset);
    if (ret != 0)
        goto fail;

    //cb_log_debug("DANDEBUG Selected modifiable node %ju -> %ju",
    //             old_curr_node_offset, s.curr_node_offset);

    curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
    curr_node->color = CB_BST_RED;
    s.new_root_node_offset = s.curr_node_offset;
    cmp = cb_key_cmp(key, &(curr_node->key));
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
        ret = cb_bst_delete_fix_root(cb, &s);
        if (ret != 0)
            goto fail;
    }

    /* Skip parent_node update on first iteration, there is no parent. */
    goto entry;

    while (s.curr_node_offset != CB_BST_SENTINEL)
    {
        //old_curr_node_offset = s.curr_node_offset;
        ret = cb_bst_select_modifiable_node(cb,
                                            cutoff_offset,
                                            &s.curr_node_offset);
        if (ret != 0)
            goto fail;

        //cb_log_debug("DANDEBUG Selected modifiable node %ju -> %ju",
        //             old_curr_node_offset, s.curr_node_offset);

        parent_node = cb_bst_node_at(*cb, s.parent_node_offset);
        parent_node->child[s.parent_to_curr_dir] = s.curr_node_offset;
#if 0
        cb_log_debug("DANDEBUG BEGIN ITERATION %u TREE:", iter);
        cb_bst_print(*cb, s.new_root_node_offset);
        cb_log_debug("DANDEBUG END ITERATION %u TREE:", iter);
        iter++;
#endif

        /* Our parent should always be red, unless curr itself is red. */
        cb_assert(cb_bst_node_is_red(*cb, s.parent_node_offset) ||
               cb_bst_node_is_red(*cb, s.curr_node_offset));

        cb_assert(cb_bst_node_is_modifiable(s.curr_node_offset, cutoff_offset));
        curr_node = cb_bst_node_at(*cb, s.curr_node_offset);
        cmp = cb_key_cmp(key, &(curr_node->key));
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
            cb_log_debug("DANDEBUG case0a @ %ju", (uintmax_t)s.curr_node_offset);
            goto descend;
        }

        /* CASE 0b - "Child-To-Descend-To is Red" */
        cb_assert(curr_node->color == CB_BST_BLACK);
        if (cb_bst_node_is_red(*cb, curr_node->child[s.dir]))
        {
            cb_log_debug("DANDEBUG case0b @ %ju", (uintmax_t)s.curr_node_offset);
            goto descend;
        }

        /* CASE 1 - "Child-To-Descend-To's Sibling is Red */
        if (cb_bst_node_is_red(*cb, curr_node->child[!s.dir]))
        {
            ret = cb_bst_delete_case1(cb, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 2  - "Current's Near Nephew is Red" */
        /* Since curr is a non-sentinel black node, it must have a sibling. */
        s.sibling_node_offset =
            cb_bst_node_at(*cb, s.parent_node_offset)->child[!s.parent_to_curr_dir];
        cb_assert(s.sibling_node_offset != CB_BST_SENTINEL);
        if (cb_bst_node_is_red(*cb,
                cb_bst_node_at(*cb, s.sibling_node_offset)->child[s.parent_to_curr_dir]))
        {
            ret = cb_bst_delete_case2(cb, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 4 - "Current's Far Nephew is Red" */
        cb_assert(s.sibling_node_offset != CB_BST_SENTINEL);
        if (cb_bst_node_is_red(*cb,
                cb_bst_node_at(*cb, s.sibling_node_offset)->child[!s.parent_to_curr_dir]))
        {
            ret = cb_bst_delete_case4(cb, &s);
            if (ret != 0)
                goto fail;

            goto descend;
        }

        /* CASE 5 - "Sibling and Its Children are Black" */
        if (cb_bst_node_is_black(*cb, s.sibling_node_offset))
        {
            ret = cb_bst_delete_case5(cb, &s);
            if (ret != 0)
                goto fail;
        }

        if (cb_bst_node_is_black(*cb, s.curr_node_offset) &&
            cb_bst_node_is_red(*cb, s.parent_node_offset) &&
            cb_bst_node_is_red(*cb, s.parent_node_offset))
        {
            cb_log_debug("DANDEBUG WTF - Unhandled EC case.");
            abort();
        }

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

#if 0
    cb_log_debug("DANDEBUG BEGIN ITERATION DONE TREE (found_node_offset: %ju)", found_node_offset);
    cb_bst_print(*cb, s.new_root_node_offset);
    cb_log_debug("DANDEBUG END ITERATION DONE TREE");
#endif

    /*FIXME we break the loop at a sentinel, having unnecessarily created
      a modifiable copy of the preceding red "leaf", only to remove it.
      Optimize this. */
    if (found_node_offset == CB_BST_SENTINEL)
    {
        /* If we haven't found a node with the given key, abort the work we
           did, including the creation of a new node path, by rewinding the
           cursor. */
        ret = -1;
        goto fail;
    }

    cb_assert(s.parent_node_offset != CB_BST_SENTINEL);
    cb_assert(s.curr_node_offset == CB_BST_SENTINEL);
    cb_assert(cb_bst_node_is_red(*cb, s.parent_node_offset));
    cb_assert(cb_bst_node_is_black(*cb, s.grandparent_node_offset) ||
           s.grandparent_node_offset == s.new_root_node_offset);


    if (found_node_offset != s.parent_node_offset)
    {
        struct cb_bst_node *found_node;
        struct cb_bst_node *to_delete_node;

        found_node     = cb_bst_node_at(*cb, found_node_offset);
        to_delete_node = cb_bst_node_at(*cb, s.parent_node_offset);
        cb_key_assign(&(found_node->key), &(to_delete_node->key));
        cb_value_assign(&(found_node->value), &(to_delete_node->value));
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
        cb_log_debug("DANDEBUG assigning CB_BST_SENTINEL to root-node");
        s.new_root_node_offset = CB_BST_SENTINEL;
    }
    else
    {
        cb_log_debug("DANDEBUG assigning black to root-node @ %ju", s.new_root_node_offset);
        root_node = cb_bst_node_at(*cb, s.new_root_node_offset);
        root_node->color = CB_BST_BLACK;
    }

    *root_node_offset = s.new_root_node_offset;

    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "post-delete-success"));
#if 0
    cb_log_debug("DANDEBUG BEGIN POST-DELETE");
    cb_bst_print(*cb, *root_node_offset);
    cb_log_debug("DANDEBUG END POST-DELETE");
#endif
    return 0;

fail:
    cb_log_debug("DANDEBUG FAILED DELETE, new root: %ju", *root_node_offset);
    cb_rewind_to(*cb, initial_cursor_offset);
    if (ret == 0)
    {
        cb_log_debug("DANDEBUG Unknown implementation error.");
        ret = -1; /* "Unknown implementation error." */
    }
    cb_heavy_assert(cb_bst_validate(*cb, *root_node_offset, "post-delete-fail"));
    return ret;
}


int
cb_map_init(struct cb_map *cb_map, struct cb **cb)
{
    cb_map->cb = cb;

    return cb_start_data_set(cb_map);
}


int
cb_map_kv_set(struct cb_map         *cb_map,
              const struct cb_key   *k,
              const struct cb_value *v)
{
    cb_offset_t command_offset;
    struct cb_command_any *command;
    int ret;

    ret = cb_command_alloc(cb_map->cb, &command_offset, &command);
    if (ret != 0)
        return ret;

    command->type = CB_CMD_KEYVAL;
    cb_key_assign(&(command->keyval.key), k);
    cb_value_assign(&(command->keyval.value), v);

    command->prev = cb_map->last_command_offset;
    cb_map->last_command_offset = command_offset;

    return 0;
}


int
cb_map_kv_lookup(const struct cb_map *cb_map,
                 const struct cb_key *k,
                 struct cb_value     *v)
{
    struct cb_command_any *cmd;
    bool did_find = false;

    cb_validate2(*(cb_map->cb));

    cmd = cb_at(*(cb_map->cb), cb_map->last_command_offset);

    while (!did_find)
    {
        switch (cmd->type)
        {
            case CB_CMD_KEYVAL:
                if (cb_key_cmp(k, &(cmd->keyval.key)) == 0)
                {
                    *v = cmd->keyval.value;
                    did_find = true;
                    goto done;
                }
                break;

            case CB_CMD_DELETEKEY:
                if (cb_key_cmp(k, &(cmd->deletekey.key)) == 0)
                    goto done;
                break;

            case CB_CMD_BST:
                if (cb_bst_lookup(*(cb_map->cb), cmd->bst.root_node_offset, k, v) == 0)
                {
                    did_find = true;
                    goto done;
                }
                break;

            case CB_CMD_START_DATA:
                goto done;

            default:
                cb_log_error("Unrecognized CB_CMD: %jd", (intmax_t)cmd->type);
        }

        cmd = cb_at(*(cb_map->cb), cmd->prev);
    }

done:
    return did_find ? 0 : -1;
}


int
cb_map_kv_delete(struct cb_map *cb_map, const struct cb_key *k)
{
    cb_offset_t command_offset;
    struct cb_command_any *command;
    int ret;

    ret = cb_command_alloc(cb_map->cb, &command_offset, &command);
    if (ret != 0)
        return ret;

    command->type = CB_CMD_DELETEKEY;
    cb_key_assign(&(command->deletekey.key), k);

    command->prev = cb_map->last_command_offset;
    cb_map->last_command_offset = command_offset;

    return 0;
}


static int
cb_map_traverse_internal(const struct cb        *cb,
                         cb_offset_t             root_node_offset,
                         cb_map_traverse_func_t  func,
                         void                   *closure)
{
    struct cb_bst_iter iter;
    cb_offset_t        curr_node_offset;
    int ret;

    curr_node_offset = root_node_offset;
    if (curr_node_offset == CB_BST_SENTINEL)
        return 0;

    iter.depth = 0;

traverse_left:
    while (curr_node_offset != CB_BST_SENTINEL)
    {
        //cb_log_debug("curr_node_offset: %ju", (uintmax_t)curr_node_offset);
        iter.finger[iter.depth].offset = curr_node_offset;
        iter.finger[iter.depth].node   = cb_bst_node_at(cb, curr_node_offset);
        curr_node_offset = iter.finger[iter.depth].node->child[0];
        iter.depth++;
    }

    /* Visit the top item from the stack. */
    ret = func(&(iter.finger[iter.depth - 1].node->key),
               &(iter.finger[iter.depth - 1].node->value),
               closure);
    if (ret != 0)
        return ret;

    curr_node_offset = iter.finger[iter.depth - 1].node->child[1];
    iter.depth--;
    if (curr_node_offset != CB_BST_SENTINEL || iter.depth != 0)
        goto traverse_left;

    return 0;
}


/*
 * This will traverse each entry in the map, from lowest to highest.
 */
int
cb_map_traverse(struct cb_map          *cb_map,
                cb_map_traverse_func_t  func,
                void                   *closure)
{
    struct cb_command_any *cmd;

    cb_validate2(*(cb_map->cb));

    cmd = cb_at(*(cb_map->cb), cb_map->last_command_offset);
    if (cmd->type != CB_CMD_BST)
    {
        cb_log_error("Cannot traverse a non-consolidated map.");
        return -1;
    }

    return cb_map_traverse_internal(*(cb_map->cb),
                                    cmd->bst.root_node_offset,
                                    func,
                                    closure);
}


struct traverse_state
{
    struct cb   **cb;
    cb_offset_t   new_root_node_offset;
    cb_offset_t   cutoff_offset;
};


static int
traversal_insert(const struct cb_key   *k,
                 const struct cb_value *v,
                 void                  *closure)
{
    struct traverse_state *ts = closure;

    return cb_bst_insert(ts->cb,
                         &(ts->new_root_node_offset),
                         ts->cutoff_offset,
                         k,
                         v);
}


static int
traversal_delete(const struct cb_key   *k,
                 const struct cb_value *v,
                 void                  *closure)
{
    struct traverse_state *ts = closure;
    int ret;

    (void)v;

    ret = cb_bst_delete(ts->cb,
                        &(ts->new_root_node_offset),
                        ts->cutoff_offset,
                        k);

    //FIXME distinguish between "not found" errors (which are fine here and
    //should return 0), vs memory allocation errors (which should return -1).
    (void)ret;

    return 0;
}


/*
 * This integrates CB_CMD_KEYVAL and CB_CMD_DELETEKEY entries into a newly-built
 * CB_CMD_BST.  Presently, this will integrate back until it reaches
 * CB_CMD_START_DATA, or an earlier CB_CMD_BST, at which point the new
 * CB_CMD_BST will link to that command as prev.  (FIXME) In the future,
 * upon reaching a CB_CMD_BST, the newly-built tree and the old tree should
 * be merged/joined/concatenated.
 */
static int
cb_map_consolidate_internal(struct cb_map *cb_map)
{
    struct cb_command_any *cmd, *new_cmd;
    cb_offset_t insertions_root_node_offset = CB_BST_SENTINEL,
                deletions_root_node_offset = CB_BST_SENTINEL,
                cmd_offset,
                new_cmd_offset,
                initial_cursor_offset,
                cutoff_offset;
    int ret;

    cb_validate2(*(cb_map->cb));

    initial_cursor_offset = cb_cursor(*(cb_map->cb));

    cmd_offset = cb_map->last_command_offset;
    cmd        = cb_at(*(cb_map->cb), cmd_offset);

    /* Our cutoff for modifiable nodes will be any node at an offset greater
       than the cursor where we have started this consolidation. */
    cutoff_offset = initial_cursor_offset;

    while (true)
    {
        switch (cmd->type)
        {
            case CB_CMD_KEYVAL:
            {
                /* Add to the new BST, unless in deletions BST. */
                if (!cb_bst_contains_key(*(cb_map->cb),
                                         deletions_root_node_offset,
                                         &(cmd->keyval.key)))
                {
                    ret = cb_bst_insert(cb_map->cb,
                                        &insertions_root_node_offset,
                                        cutoff_offset,
                                        &(cmd->keyval.key),
                                        &(cmd->keyval.value));
                    if (ret != 0)
                        goto fail;
                }
            }
            break;

            case CB_CMD_DELETEKEY:
            {
                struct cb_value bogus_value;

                /* Add to the deletions BST. */
                ret = cb_bst_insert(cb_map->cb,
                                    &deletions_root_node_offset,
                                    cutoff_offset,
                                    &(cmd->deletekey.key),
                                    &bogus_value);
                if (ret != 0)
                    goto fail;
            }
            break;

            case CB_CMD_BST:
            {
                struct traverse_state ts;

                ts.cb                   = cb_map->cb;
                ts.new_root_node_offset = cmd->bst.root_node_offset;
                ts.cutoff_offset        = cutoff_offset;

                /* Apply each accumulated deletion to the new tree. */
                ret = cb_map_traverse_internal(*(cb_map->cb),
                                               deletions_root_node_offset,
                                               traversal_delete,
                                               &ts);
                if (ret != 0)
                    goto fail;

                /* Apply each accumulated insertion to the new tree. */
                ret = cb_map_traverse_internal(*(cb_map->cb),
                                               insertions_root_node_offset,
                                               traversal_insert,
                                               &ts);
                if (ret != 0)
                    goto fail;

                /* Append a new CB_CMD_BST which refers to the new root, and
                   its prev pointing past the earlier CB_CMD_BST that we
                   have presently encountered (and which was used as the initial
                   value of the accumulator).  */
                ret = cb_command_alloc(cb_map->cb, &new_cmd_offset, &new_cmd);
                if (ret != 0)
                    goto fail;

                new_cmd->type = CB_CMD_BST;
                new_cmd->bst.root_node_offset = ts.new_root_node_offset;
                new_cmd->prev = cmd->prev;
            }
            goto done;

            case CB_CMD_START_DATA:
            {
                /* Link to as the new CB_CMD_BST's prev. */
                ret = cb_command_alloc(cb_map->cb, &new_cmd_offset, &new_cmd);
                if (ret != 0)
                    goto fail;

                new_cmd->type = CB_CMD_BST;
                new_cmd->bst.root_node_offset = insertions_root_node_offset;
                new_cmd->prev = cmd_offset;
            }
            goto done;

            default:
                cb_log_error("Unrecognized CB_CMD: %jd", (intmax_t)cmd->type);
                goto fail;
        }

        cmd_offset = cmd->prev;
        cmd        = cb_at(*(cb_map->cb), cmd_offset);
    }

done:
    cb_map->last_command_offset = new_cmd_offset;
    return 0;

fail:
    cb_rewind_to(*(cb_map->cb), initial_cursor_offset);
    return ret;
}


int
cb_map_consolidate(struct cb_map *cb_map)
{
    int ret;

#if 0
    cb_log_debug("=====BEGIN CONSOLIDATION=====");
    cb_log_debug("---BEGIN STATE 0---");
    cb_map_print(cb_map);
    cb_log_debug("---END STATE 0---");
#endif

    ret = cb_map_consolidate_internal(cb_map);

#if 0
    cb_log_debug("---BEGIN STATE 1---");
    cb_map_print(cb_map);
    cb_log_debug("---END STATE 1---");
    cb_log_debug("=====END CONSOLIDATION%s=====", ret != 0 ? " (FAIL)" : "");
#endif

    return ret;
}


static char*
cb_key_to_str(struct cb_key *key)
{
    char *str;
    int ret;

    ret = asprintf(&str, "%" PRIu64, key->k);
    return (ret == -1 ? NULL : str);
}


static char*
cb_value_to_str(struct cb_value *value)
{
    char *str;
    int ret;

    ret = asprintf(&str, "%" PRIu64, value->v);
    return (ret == -1 ? NULL : str);
}


static char*
cb_bst_to_str(const struct cb *cb, cb_offset_t node_offset)
{
    struct cb_bst_node *node;
    char *str, *keystr, *valstr, *leftstr, *rightstr;
    int ret;

    node = cb_bst_node_at(cb, node_offset);
    if (!node)
        return strdup("NIL");

    keystr   = cb_key_to_str(&(node->key));
    valstr   = cb_value_to_str(&(node->value));
    leftstr  = cb_bst_to_str(cb, node->child[0]);
    rightstr = cb_bst_to_str(cb, node->child[1]);

    ret = asprintf(&str, "(%s %s=%s %s)", leftstr, keystr, valstr, rightstr);

    free(keystr);
    free(valstr);
    free(leftstr);
    free(rightstr);

    return (ret == -1 ? NULL : str);
}


void
cb_map_print(const struct cb_map *cb_map)
{
    cb_offset_t            cmd_offset;
    struct cb_command_any *cmd;

    cmd_offset = cb_map->last_command_offset;
    cmd        = cb_at(*(cb_map->cb), cmd_offset);

    while (true)
    {
        switch (cmd->type)
        {
            case CB_CMD_KEYVAL:
            {
                char *key_str, *val_str;

                key_str = cb_key_to_str(&(cmd->keyval.key));
                val_str = cb_value_to_str(&(cmd->keyval.value));
                printf("[%ju (+%ju)]\tKEYVAL %s = %s\n",
                       (uintmax_t)cmd_offset,
                       (uintmax_t)(cmd_offset - cmd->prev),
                       key_str, val_str);
                free(key_str);
                free(val_str);
            }
            break;

            case CB_CMD_DELETEKEY:
            {
                char *key_str;

                key_str = cb_key_to_str(&(cmd->deletekey.key));
                printf("[%ju (+%ju)]\tDELETEKEY %s\n",
                       (uintmax_t)cmd_offset,
                       (uintmax_t)(cmd_offset - cmd->prev),
                       key_str);
                free(key_str);
            }
            break;

            case CB_CMD_BST:
            {
                printf("[%ju (+%ju)]\tBST (root_node_offset: %ju):\n",
                       (uintmax_t)cmd_offset,
                       (uintmax_t)(cmd_offset - cmd->prev),
                       (uintmax_t)cmd->bst.root_node_offset);
                cb_bst_print(*(cb_map->cb), cmd->bst.root_node_offset);
#if 0
                char *bst_str;

                bst_str = cb_bst_to_str(*(cb_map->cb), cmd->bst.root_node_offset);
                printf("[%ju (+%ju)]\tBST %s\n",
                       (uintmax_t)cmd_offset,
                       (uintmax_t)(cmd_offset - cmd->prev),
                       bst_str);
                free(bst_str);
#endif
            }
            break;

            case CB_CMD_START_DATA:
                printf("[%ju]\tDATA_START\n",
                       (uintmax_t)cmd_offset);
                goto done;

            default:
                printf("CB_CMD??? %jd", (intmax_t)cmd->type);
        }

        cmd_offset = cmd->prev;
        cmd        = cb_at(*(cb_map->cb), cmd_offset);
    }

done:
    return;
}
