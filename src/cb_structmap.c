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
#include "cb_structmap.h"

#include "cb_assert.h"
#include "cb_bits.h"

#include <string.h>


/* FIXME
 * 1) Allow CB_STRUCTMAP_LEVEL_BITS in the range of 2..8.  This will involve
 *    changing child_locations to an array.
 * 2) Use appropriate type sizes for the stack variables used for math (either
 *    minimal or arch-convenient).
 * 4) Do proper debug and error lines for error cases.  Check values of ret.
 * 5) child_locations is redundant for CB_STRUCTMAP_SPARSE layouts.  Remove?
 * 6) Remove parent node if last child is removed.
 */

#ifndef CB_STRUCTMAP_LEVEL_BITS
#    define CB_STRUCTMAP_LEVEL_BITS    5
#endif

#if (CB_STRUCTMAP_LEVEL_BITS != 4) && \
    (CB_STRUCTMAP_LEVEL_BITS != 5) && \
    (CB_STRUCTMAP_LEVEL_BITS != 6)
#    error "That's a crazy number for CB_STRUCTMAP_LEVEL_BITS!"
//FIXME, actually we should probably allow 2 to 8. 7 and 8 would involve
//child_locations being broken into several fields
#endif


enum cb_structmap_layout
{
    CB_STRUCTMAP_SPARSE,
    CB_STRUCTMAP_CONDENSED
};


struct cb_structmap_node
{
    enum cb_structmap_layout layout;
    uint8_t                  consume_bitcount;
    uint8_t                  enclosed_bitcount;
    uint64_t                 child_locations;
    cb_offset_t              children[];
};


static inline bool
cb_structmap_node_is_leaf(struct cb_structmap_node *node)
{
    return (node->enclosed_bitcount <= CB_STRUCTMAP_LEVEL_BITS);
}


static inline bool
cb_structmap_node_has_child(struct cb_structmap_node *node,
                            uint8_t                   child_index)
{
    //FIXME, until we rework child_locations, we can't exceed 64 indices
    cb_assert(child_index < 64);
    return !!(node->child_locations & ((uint64_t)1 << child_index));
}


static inline cb_offset_t
cb_structmap_node_child(struct cb_structmap_node *node,
                        uint8_t                   child_index)
{
    size_t condensed_index;
    cb_assert(cb_structmap_node_has_child(node, child_index));
    condensed_index =
        popcount64(node->child_locations & (((uint64_t)1 << child_index) - 1));
    return node->children[condensed_index];
}


static int
cb_structmap_node_alloc(struct cb   **cb,
                        cb_offset_t  *node_offset,
                        uint8_t       entry_count)
{
    cb_offset_t new_node_offset;
    int ret;

    ret = cb_memalign(cb,
                      &new_node_offset,
                      cb_alignof(struct cb_structmap_node),
                      (sizeof(struct cb_structmap_node) +
                        (entry_count * sizeof(cb_offset_t))));
    if (ret != 0)
        return ret;

    *node_offset = new_node_offset;

    return 0;
}


static inline struct cb_structmap_node*
cb_structmap_node_at(const struct cb *cb,
                     cb_offset_t      node_offset)
{
    if (node_offset == CB_STRUCTMAP_SENTINEL)
        return NULL;

    return (struct cb_structmap_node*)cb_at(cb, node_offset);
}


static inline bool
cb_structmap_node_is_modifiable(cb_offset_t node_offset,
                          cb_offset_t cutoff_offset)
{
    int cmp = cb_offset_cmp(node_offset, cutoff_offset);
    cb_assert(cmp == -1 || cmp == 0 || cmp == 1);
    return cmp > -1;
}


static int
cb_structmap_select_modifiable_node(struct cb          **cb,
                                    cb_offset_t          cutoff_offset,
                                    cb_offset_t         *node_offset)
{
    /* If the node we are trying to modify has been freshly created, then it is
       safe to modify it in place.  Otherwise, a copy will be made and we will
       need to rewrite its parentage.  The cutoff_offset is the offset at which
       the unmodifiable to modifiable transition happens, with nodes prior
       to the offset requiring a new node to be allocated. */
    cb_offset_t               old_node_offset,
                              new_node_offset;
    struct cb_structmap_node *old_node,
                             *new_node;
    int ret;

    old_node_offset = *node_offset;

    if (cb_structmap_node_is_modifiable(old_node_offset, cutoff_offset))
        return 0;

    /* The provided node is unmodifiable and must be copied. */
    old_node = cb_structmap_node_at(*cb, old_node_offset);

    /*
     * Create a new cb_structmap_node with as many children as the branch
     * factor of 'consume_bitcount' dictates are necessary.
     */
    ret = cb_structmap_node_alloc(cb,
                                  &new_node_offset,
                                  1 << old_node->consume_bitcount);
    if (ret != 0)
        return ret;

    new_node = cb_structmap_node_at(*cb, new_node_offset);
    if (old_node->layout == CB_STRUCTMAP_SPARSE)
    {
        /*
         * The source is sparse, and modifiable nodes are always sparse.  They
         * will be bitwise identical, so a simple memcpy() suffices.
         */
        memcpy(new_node, old_node, sizeof(struct cb_structmap_node) +
                ((1 << old_node->consume_bitcount) * sizeof(cb_offset_t)));
    }
    else
    {
        /*
         * The source is condensed.  We must convert its contents to a sparse
         * layout as we copy it to a modifiable node.
         */
        uint8_t num_entries = (1 << old_node->consume_bitcount);

        cb_assert(old_node->layout == CB_STRUCTMAP_CONDENSED);
        new_node->layout            = CB_STRUCTMAP_SPARSE;
        new_node->consume_bitcount  = old_node->consume_bitcount;
        new_node->enclosed_bitcount = old_node->enclosed_bitcount;
        new_node->child_locations   = (num_entries == 64 ? UINT64_MAX
                                      : ((uint64_t)1 << num_entries) - 1);
        for (int i = 0; i < num_entries; ++i)
        {
            if (cb_structmap_node_has_child(old_node, i))
                new_node->children[i] = cb_structmap_node_child(old_node, i);
            else
                new_node->children[i] = CB_STRUCTMAP_SENTINEL;
        }
    }

    *node_offset = new_node_offset;

    return 0;
}


static void
cb_structmap_print_internal(const struct cb *cb,
                            cb_offset_t      node_offset,
                            uint8_t          depth)
{
    struct cb_structmap_node *node;
    int                       max_index;
    static char spaces[] = "\t\t\t\t\t\t\t\t"
                           "\t\t\t\t\t\t\t\t";

    cb_assert(node_offset != CB_STRUCTMAP_SENTINEL);
    cb_assert(depth <= 16);

    node = cb_structmap_node_at(cb, node_offset);
    cb_assert(node != NULL);

    printf("%.*s{%d}@%ju (%s, levelbits:%d, enclosed_bits:%ju, children:0x%jx)\n",
           (int)depth, spaces,
           (int)depth,
           (uintmax_t)node_offset,
           node->layout == CB_STRUCTMAP_SPARSE ? "sparse" : "condensed",
           (int)node->consume_bitcount,
           (uintmax_t)node->enclosed_bitcount,
           (uintmax_t)node->child_locations);

    max_index = (1 << node->consume_bitcount);
    cb_assert(max_index <= 64);

    if (!cb_structmap_node_is_leaf(node))
    {
        /* Print an intermediate level, vertically. */
        for (int i = 0; i < max_index; ++i)
        {
            cb_offset_t child_offset;

            if (!cb_structmap_node_has_child(node, i))
            {
                printf("%.*s{%d}@%ju[%d]=omit\n",
                       (int)depth, spaces,
                       (int)depth,
                       (uintmax_t)node_offset,
                       i);
                continue;
            }

            child_offset = cb_structmap_node_child(node, i);
            if (child_offset == CB_STRUCTMAP_SENTINEL)
            {
                printf("%.*s{%d}@%ju[%d]=nil\n",
                       (int)depth, spaces,
                       (int)depth,
                       (uintmax_t)node_offset,
                       i);
            }
            else
            {
                printf("%.*s{%d}@%ju[%d]=subtree@%ju\n",
                       (int)depth, spaces,
                       (int)depth,
                       (uintmax_t)node_offset,
                       i,
                       (uintmax_t)child_offset);

                cb_structmap_print_internal(cb, child_offset, depth + 1);
            }
        }
    }
    else
    {
        /* Print a leaf level, horizontally. */
        bool needs_comma = false;

        printf("%.*s", (int)depth, spaces);

        for (int i = 0; i < max_index; ++i)
        {
            cb_offset_t child_offset;

            if (!cb_structmap_node_has_child(node, i))
            {
                printf("%s[%d]=omit", needs_comma ? "," : "", i);
                continue;
            }

            child_offset = cb_structmap_node_child(node, i);
            if (child_offset == CB_STRUCTMAP_SENTINEL)
                printf("%s[%d]=nil", needs_comma ? "," : "", i);
            else
            {
                printf("%s[%d]=%ju",
                       needs_comma ? "," : "",
                       i,
                       (uintmax_t)child_offset);
            }

            needs_comma = true;
        }

        printf("\n");
    }
}


void
cb_structmap_print(const struct cb *cb,
                   cb_offset_t      node_offset)
{
    cb_structmap_print_internal(cb, node_offset, 0);
}


static int
cb_structmap_heighten(struct cb      **cb,
                      cb_offset_t     *root_node_offset,
                      uint64_t         enclosed_bitcount,
                      cb_struct_id_t   struct_id)
{
    /*
     * We have some bits in use in the provided struct_id which exceed the
     * range represented by this structmap.  We need to extend the range by
     * building new levels for these bits (consuming CB_STRUCTMAP_LEVEL_BITS
     * of this excess per new level) on top of the existing structmap.  If
     * CB_STRUCT_LEVEL_BITS does not evenly divide the uint64_t used to
     * represent a cb_struct_id_t, then we will also need to bound the bits
     * consumed per level by the headroom.
     */
    cb_offset_t               curr_root_node_offset;
    cb_offset_t               lower_level_node_offset;
    uint64_t                  unenclosed_bits_remaining;
    int                       headroom_bitcount;
    int ret;

    curr_root_node_offset     = *root_node_offset;
    lower_level_node_offset   = curr_root_node_offset;
    headroom_bitcount         = 64 - enclosed_bitcount;
    /*NOTE: (struct_id | 1) guarantees proper behavior when struct_id == 0. */
    unenclosed_bits_remaining = ((struct_id | 1) >> enclosed_bitcount);
    cb_assert(headroom_bitcount > 0);
    cb_assert(headroom_bitcount <= 64);
    cb_assert(unenclosed_bits_remaining > 0);
    cb_assert(headroom_bitcount == 64 ||
           ((uint64_t)1 << headroom_bitcount) >= unenclosed_bits_remaining);

    while (unenclosed_bits_remaining > 0)
    {
        struct cb_structmap_node *new_level_node;
        cb_offset_t               new_level_node_offset;
        uint64_t                  new_level_enclosed_bitcount;
        uint8_t                   new_level_consume_bitcount,
                                  new_level_num_entries;

        new_level_consume_bitcount = CB_STRUCTMAP_LEVEL_BITS;
        if (headroom_bitcount < CB_STRUCTMAP_LEVEL_BITS)
            new_level_consume_bitcount = headroom_bitcount;
        new_level_num_entries = (1 << new_level_consume_bitcount);
        cb_assert(new_level_num_entries <= 64);
        new_level_enclosed_bitcount =
            (enclosed_bitcount + new_level_consume_bitcount);

        ret = cb_structmap_node_alloc(cb,
                                      &new_level_node_offset,
                                      new_level_num_entries);
        if (ret != 0)
            return ret;

        new_level_node = cb_structmap_node_at(*cb, new_level_node_offset);
        cb_assert(new_level_node);

        /*
         * Initialize new level's node contents. The earlier, lower
         * levels will always be down the 0th path.
         */
        new_level_node->layout = CB_STRUCTMAP_SPARSE;
        new_level_node->consume_bitcount  = new_level_consume_bitcount;
        new_level_node->enclosed_bitcount = new_level_enclosed_bitcount;
        new_level_node->child_locations =
            (new_level_num_entries == 64 ? UINT64_MAX
             : ((uint64_t)1 << new_level_num_entries) - 1);
        new_level_node->children[0] = lower_level_node_offset;
        for (int i = 1; i < new_level_num_entries; ++i)
            new_level_node->children[i] = CB_STRUCTMAP_SENTINEL;

        /* Iterate. */
        lower_level_node_offset   =   new_level_node_offset;
        unenclosed_bits_remaining >>= new_level_consume_bitcount;
        headroom_bitcount         -=  new_level_consume_bitcount;
        curr_root_node_offset     =   new_level_node_offset;
        enclosed_bitcount         =   new_level_enclosed_bitcount;
    }

    /*
     * After the above level-creation work, we should now be pointing
     * to a root structmap node which is sufficiently enclosing of the
     * provided struct_id.
     */
    cb_assert(curr_root_node_offset != CB_STRUCTMAP_SENTINEL);
    cb_assert(enclosed_bitcount ==
           cb_structmap_node_at(*cb, curr_root_node_offset)->enclosed_bitcount);
    cb_assert(enclosed_bitcount == 64 ||
            struct_id <= ((uint64_t)1 << enclosed_bitcount));

    *root_node_offset = curr_root_node_offset;

    return 0;
}


static bool
cb_structmap_validate(const struct cb *cb,
                      cb_offset_t      node_offset,
                      const char      *name)
{
    struct cb_structmap_node *node;
    bool layout_ok            = false,
         consume_bitcount_ok  = false,
         enclosed_bitcount_ok = false,
         children_ok          = false;
    size_t num_children;

    if (node_offset == CB_STRUCTMAP_SENTINEL)
        return true;

    node = cb_structmap_node_at(cb, node_offset);

    /* Validate layout type. */
    layout_ok = (node->layout == CB_STRUCTMAP_SPARSE ||
                 node->layout == CB_STRUCTMAP_CONDENSED);
    if (!layout_ok)
    {
        cb_log_error("Bad layout %d for structmap node @ %ju, %s",
                     node->layout, (uintmax_t)node_offset, name);
        goto done;
    }

    /* Validate consume_bitcount. */
    consume_bitcount_ok = (node->consume_bitcount > 0 &&
                           node->consume_bitcount <= CB_STRUCTMAP_LEVEL_BITS);
    if (!consume_bitcount_ok)
    {
        cb_log_error("Bad consume_bitcount (%ju) for structmap node @ %ju",
                     (uintmax_t)node->consume_bitcount,
                     (uintmax_t)node_offset);
        goto done;
    }

    num_children = ((size_t)1 << node->consume_bitcount);

    /* Validate enclosed_bitcount. */
    if (cb_structmap_node_is_leaf(node))
    {
        /*
         * Leaf nodes have no children for their enclosed_bitcount to conflict
         * with.
         */
        enclosed_bitcount_ok = true;
    }
    else
    {
        /*
         * We're validating an intermediate node, check that all children have
         * a shallower enclosed_bitcount.
         */
        enclosed_bitcount_ok = true;
        for (size_t i = 0; i < num_children; ++i)
        {
            cb_offset_t               child_node_offset;
            struct cb_structmap_node *child_node;
            bool                      child_enclosed_bitcount_ok;

            if (!cb_structmap_node_has_child(node, i))
                continue;

            child_node_offset = cb_structmap_node_child(node, i);
            if (child_node_offset  == CB_STRUCTMAP_SENTINEL)
                continue;

            child_node = cb_structmap_node_at(cb, child_node_offset);

            child_enclosed_bitcount_ok =
                (child_node->enclosed_bitcount < node->enclosed_bitcount);
            if (!child_enclosed_bitcount_ok)
            {
                cb_log_error("node @ %ju has child node @ %ju with too-large"
                             " enclosed mask. (node enclosed_bitcount: %ju,"
                             " child node enclosed_bitcount: %ju)",
                             (uintmax_t)node_offset,
                             (uintmax_t)child_node_offset,
                             (uintmax_t)node->enclosed_bitcount,
                             (uintmax_t)child_node->enclosed_bitcount);
            }

            enclosed_bitcount_ok &= child_enclosed_bitcount_ok;
        }
    }

    /* Recursively validate children. */
    children_ok = true;
    if (!cb_structmap_node_is_leaf(node)) {
        for (size_t i = 0; i < num_children; ++i)
        {
            cb_offset_t child_offset;

            if (!cb_structmap_node_has_child(node, i))
                continue;

            child_offset = cb_structmap_node_child(node, i);
            children_ok &= cb_structmap_validate(cb, child_offset, name);
        }
    }

done:
    return layout_ok & consume_bitcount_ok & enclosed_bitcount_ok & children_ok;
}


int
cb_structmap_insert(struct cb      **cb,
                    cb_offset_t     *root_node_offset,
                    cb_offset_t      cutoff_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t      struct_offset)
{
    cb_offset_t               initial_cursor_offset = cb_cursor(*cb),
                              new_root_node_offset,
                              curr_node_offset,
                              child_node_offset;
    struct cb_structmap_node *curr_node,
                             *child_node;
    uint8_t                   curr_path_to_child;
    uint64_t                  enclosed_bitcount = 0;
    uint64_t                  enclosed_mask = 0;
    uint64_t                  enclosed_bitcount_remaining;
    int ret;

    cb_heavy_assert(cb_structmap_validate(*cb, *root_node_offset, "pre-insert"));

    curr_node_offset = *root_node_offset;

    /* The tree may be empty.  If so, we must heighten the tree. */
    if (curr_node_offset == CB_STRUCTMAP_SENTINEL)
            goto heighten;

    /*
     * The tree may not yet represent a range inclusive of this struct_id.  If
     * so, we must heighten the tree.
     */
    enclosed_bitcount =
        cb_structmap_node_at(*cb, curr_node_offset)->enclosed_bitcount;
    enclosed_mask = (enclosed_bitcount == 64 ? UINT64_MAX :
            (((uint64_t)1 << enclosed_bitcount) - 1));
    if (struct_id > enclosed_mask)
    {
heighten:
        ret = cb_structmap_heighten(cb,
                                    &curr_node_offset,
                                    enclosed_bitcount,
                                    struct_id);
        if (ret != 0)
            goto fail;
    }

    /*
     * We now have a tree whose range suffices for enclosing the struct_id
     * being requested to be stored.  We just need to take a path-copying
     * approach until we reach the leaf where we will store the struct_offset.
     */
    ret = cb_structmap_select_modifiable_node(cb,
                                              cutoff_offset,
                                              &curr_node_offset);
    if (ret != 0)
        goto fail;

    curr_node = cb_structmap_node_at(*cb, curr_node_offset);

    new_root_node_offset = curr_node_offset;

    enclosed_bitcount_remaining = curr_node->enclosed_bitcount;
    cb_assert(enclosed_bitcount_remaining > 0);
    cb_assert(enclosed_bitcount_remaining >= curr_node->consume_bitcount);
    while (enclosed_bitcount_remaining > curr_node->consume_bitcount)
    {
        curr_path_to_child =
            bits_at(struct_id,
                    curr_node->consume_bitcount,
                    enclosed_bitcount_remaining - curr_node->consume_bitcount);

        if (curr_node->children[curr_path_to_child] == CB_STRUCTMAP_SENTINEL)
        {
            uint8_t child_num_entries;

            /* Allocate new child node. */
            child_num_entries = (1 << CB_STRUCTMAP_LEVEL_BITS);
            ret = cb_structmap_node_alloc(cb,
                                          &child_node_offset,
                                          child_num_entries);
            if (ret != 0)
                goto fail;

            /* Resample curr_node in case cb had been resized. */
            curr_node = cb_structmap_node_at(*cb, curr_node_offset);

            /*
             * Setup a new child node. Note that new nodes created here are
             * always of the CB_STRUCTMAP_SPARSE layout so that they can
             * potentially later be modified without need of resizing.  For this
             * reason, we access the children[] array directly instead of via
             * cb_structmap_node_child() which would use unnecessary popcount
             * calls.
             */
            child_node = cb_structmap_node_at(*cb, child_node_offset);
            child_node->layout = CB_STRUCTMAP_SPARSE;
            child_node->consume_bitcount = CB_STRUCTMAP_LEVEL_BITS;
            child_node->enclosed_bitcount =
                (curr_node->enclosed_bitcount - curr_node->consume_bitcount);
            child_node->child_locations = (child_num_entries == 64 ?
                    UINT64_MAX : ((uint64_t)1 << child_num_entries) - 1);
            for (int i = 0; i < child_num_entries; ++i)
                child_node->children[i] = CB_STRUCTMAP_SENTINEL;

            /* Assign new child node to current node. */
            curr_node->children[curr_path_to_child] = child_node_offset;
        }

        /* Iterate. */
        enclosed_bitcount_remaining -= curr_node->consume_bitcount;
        curr_node_offset = curr_node->children[curr_path_to_child];
        ret = cb_structmap_select_modifiable_node(cb,
                                                  cutoff_offset,
                                                  &curr_node_offset);
        if (ret != 0)
            goto fail;
        curr_node = cb_structmap_node_at(*cb, curr_node_offset);
    }

    /*
     * For the final level, we do not produce any new children.  The
     * children[i] slot is therefore not used to store a cb_offset_t of a
     * child node for the next level.  Instead, it stores the offset of the
     * struct being inserted into this structmap.
     */
    cb_assert(enclosed_bitcount_remaining == curr_node->consume_bitcount);
    curr_path_to_child = bits_at(struct_id, curr_node->consume_bitcount, 0);
    curr_node->children[curr_path_to_child] = struct_offset;

    cb_heavy_assert(cb_structmap_validate(*cb,
                                       new_root_node_offset,
                                       "post-insert"));

    /* Update the received root to point to the new root. */
    *root_node_offset = new_root_node_offset;

    return 0;

fail:
    cb_rewind_to(*cb, initial_cursor_offset);
    cb_heavy_assert(cb_structmap_validate(*cb,
                                       *root_node_offset,
                                       "post-insert-fail"));
    return ret;
}


int
cb_structmap_lookup(const struct cb *cb,
                    cb_offset_t      root_node_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t     *struct_offset)
{
    cb_offset_t               curr_node_offset;
    struct cb_structmap_node *curr_node;
    uint8_t                   curr_path_to_child;
    uint64_t                  enclosed_bitcount;
    uint64_t                  enclosed_mask;
    uint64_t                  enclosed_bitcount_remaining;

    cb_heavy_assert(cb_structmap_validate(cb,
                                       root_node_offset,
                                       "pre-lookup"));

    curr_node_offset = root_node_offset;
    if (curr_node_offset == CB_STRUCTMAP_SENTINEL)
        return -1;

    curr_node = cb_structmap_node_at(cb, curr_node_offset);
    enclosed_bitcount = curr_node->enclosed_bitcount;
    enclosed_mask = (enclosed_bitcount == 64 ? UINT64_MAX :
            (((uint64_t)1 << enclosed_bitcount) - 1));
    if (struct_id > enclosed_mask)
        return -1;

    /* Traverse for segments of the struct_id. */
    enclosed_bitcount_remaining = enclosed_bitcount;
    cb_assert(enclosed_bitcount_remaining > 0);
    cb_assert(enclosed_bitcount_remaining >= curr_node->consume_bitcount);
    while (enclosed_bitcount_remaining > curr_node->consume_bitcount)
    {
        curr_path_to_child =
            bits_at(struct_id,
                    curr_node->consume_bitcount,
                    enclosed_bitcount_remaining - curr_node->consume_bitcount);

        if (curr_node->layout == CB_STRUCTMAP_CONDENSED &&
            !cb_structmap_node_has_child(curr_node, curr_path_to_child))
        {
            return -1;
        }

        if (curr_node->children[curr_path_to_child] == CB_STRUCTMAP_SENTINEL)
            return -1;

        /* Iterate. */
        enclosed_bitcount_remaining -= curr_node->consume_bitcount;
        curr_node_offset = curr_node->children[curr_path_to_child];
        curr_node = cb_structmap_node_at(cb, curr_node_offset);
    }

    /* Check at the final leaf. */
    cb_assert(enclosed_bitcount_remaining == curr_node->consume_bitcount);
    curr_path_to_child = bits_at(struct_id, curr_node->consume_bitcount, 0);
    if (curr_node->layout == CB_STRUCTMAP_CONDENSED &&
        !cb_structmap_node_has_child(curr_node, curr_path_to_child))
    {
        return -1;
    }

    if (curr_node->children[curr_path_to_child] == CB_STRUCTMAP_SENTINEL)
        return -1;


    *struct_offset = curr_node->children[curr_path_to_child];
    return 0;
}


int
cb_structmap_delete(struct cb      **cb,
                    cb_offset_t     *root_node_offset,
                    cb_offset_t      cutoff_offset,
                    cb_struct_id_t   struct_id,
                    cb_offset_t     *struct_offset)
{
    cb_offset_t               initial_cursor_offset = cb_cursor(*cb),
                              new_root_node_offset,
                              curr_node_offset,
                              struct_offset_removed;
    struct cb_structmap_node *curr_node;
    uint8_t                   curr_path_to_child;
    uint64_t                  enclosed_bitcount = 0;
    uint64_t                  enclosed_mask = 0;
    uint64_t                  enclosed_bitcount_remaining;
    int ret;

    cb_heavy_assert(cb_structmap_validate(*cb, *root_node_offset, "pre-delete"));

    curr_node_offset = *root_node_offset;

    /* Check for empty tree. */
    if (curr_node_offset == CB_STRUCTMAP_SENTINEL)
    {
        ret = -1;
        goto fail;
    }

    /* Check for struct_id in excess of range of the tree. */
    enclosed_bitcount =
        cb_structmap_node_at(*cb, curr_node_offset)->enclosed_bitcount;
    enclosed_mask = (enclosed_bitcount == 64 ? UINT64_MAX :
            (((uint64_t)1 << enclosed_bitcount) - 1));
    if (struct_id > enclosed_mask)
    {
        ret = -1;
        goto fail;
    }

    /*
     * We now have a tree whose range we know encloses the struct_id.  Let's
     * optimistically begin path-copying down to the leaf which would contain
     * the struct_id (if it were to exist).  If ultimately the struct_id does
     * not exist, we can rewind to initial_cursor_offset.
     */
    ret = cb_structmap_select_modifiable_node(cb,
                                              cutoff_offset,
                                              &curr_node_offset);
    if (ret != 0)
        goto fail;

    curr_node = cb_structmap_node_at(*cb, curr_node_offset);

    new_root_node_offset = curr_node_offset;

    enclosed_bitcount_remaining = curr_node->enclosed_bitcount;
    cb_assert(enclosed_bitcount_remaining > 0);
    cb_assert(enclosed_bitcount_remaining >= curr_node->consume_bitcount);
    while (enclosed_bitcount_remaining > curr_node->consume_bitcount)
    {
        curr_path_to_child =
            bits_at(struct_id,
                    curr_node->consume_bitcount,
                    enclosed_bitcount_remaining - curr_node->consume_bitcount);

        /*
         * If there is no path to this struct_id, then the struct_id doesn't
         * exist in this tree.a
         */
        if (curr_node->children[curr_path_to_child] == CB_STRUCTMAP_SENTINEL)
            goto fail;


        /* Iterate. */
        enclosed_bitcount_remaining -= curr_node->consume_bitcount;
        curr_node_offset = curr_node->children[curr_path_to_child];
        ret = cb_structmap_select_modifiable_node(cb,
                                                  cutoff_offset,
                                                  &curr_node_offset);
        if (ret != 0)
            goto fail;
        curr_node = cb_structmap_node_at(*cb, curr_node_offset);
    }

    /*
     * For the final level, we do not produce any new children.  We simply
     * check whether this struct_id is stored (i.e. the appropriate children[i]
     * offset for this level is != CB_STRUCTMAP_SENTINEL), and clear it if so.
     */
    cb_assert(enclosed_bitcount_remaining == curr_node->consume_bitcount);
    curr_path_to_child = bits_at(struct_id, curr_node->consume_bitcount, 0);
    if (curr_node->children[curr_path_to_child] == CB_STRUCTMAP_SENTINEL)
    {
        ret = -1;
        goto fail;
    }
    struct_offset_removed = curr_node->children[curr_path_to_child];
    curr_node->children[curr_path_to_child] = CB_STRUCTMAP_SENTINEL;

    cb_heavy_assert(cb_structmap_validate(*cb, *root_node_offset, "post-delete"));

    /* Update the received root to point to the new root. */
    *root_node_offset = new_root_node_offset;
    /* If provided, update the struct_offset to be the removed struct offset. */
    if (struct_offset)
        *struct_offset = struct_offset_removed;
    return 0;

fail:
    cb_rewind_to(*cb, initial_cursor_offset);
    cb_heavy_assert(cb_structmap_validate(*cb,
                                       *root_node_offset,
                                       "post-delete-fail"));
    return ret;
}


int
cb_structmap_condense(struct cb      **cb,
                      cb_offset_t     *root_node_offset,
                      cb_offset_t      dest_offset)
{
    (void)cb, (void)root_node_offset, (void)dest_offset;
    return -1;
}


int
cb_structmap_cmp(const struct cb *cb,
                 cb_offset_t      lhs,
                 cb_offset_t      rhs)
{
    /*FIXME make this value-based by traversal of keys and values.  Right now
     * it is identity-based. */

    (void)cb;
    return cb_offset_cmp(lhs, rhs);
}


size_t
cb_structmap_size(const struct cb *cb,
                  cb_offset_t      root_node_offset)
{
    (void)cb;

    if (root_node_offset == CB_STRUCTMAP_SENTINEL)
        return 0;

    /* FIXME implement */
    return 0;
}


int
cb_structmap_render(cb_offset_t   *dest_offset,
                    struct cb    **cb,
                    cb_offset_t    node_offset,
                    unsigned int   flags)
{
    (void)dest_offset, (void)cb, (void)node_offset, (void)flags;
    return -1; /*FIXME Unimplemented */
}


const char*
cb_structmap_to_str(struct cb   **cb,
                    cb_offset_t   node_offset)
{
    cb_offset_t dest_offset;
    int ret;

    ret = cb_structmap_render(&dest_offset, cb, node_offset, CB_RENDER_DEFAULT);
    if (ret != 0)
        return "(render-error)";

    return (const char*)cb_at(*cb, dest_offset);
}

