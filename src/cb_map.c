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
#include "cb_map.h"

#include "cb.h"
#include "cb_assert.h"
#include "cb_bst.h"

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

    return cb_bst_traverse(*(cb_map->cb),
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
                ret = cb_bst_traverse(*(cb_map->cb),
                                      deletions_root_node_offset,
                                      traversal_delete,
                                      &ts);
                if (ret != 0)
                    goto fail;

                /* Apply each accumulated insertion to the new tree. */
                ret = cb_bst_traverse(*(cb_map->cb),
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
