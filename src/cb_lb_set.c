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
#include "cb_lb_set.h"


static int
lbentry_cmp(struct cb_lb_entry *lhs,
            struct cb_lb_entry *rhs)
{
    return cb_offset_cmp(lhs->lower_bound, rhs->lower_bound);
}


/* Generate the required red-black tree helper functions */
RB_GENERATE_STATIC(cb_lb_tree, cb_lb_entry, linkage, lbentry_cmp);


static bool
cb_lb_set_validate(struct cb_lb_set *lbset)
{
    uint64_t            num_entries = 0;
    struct cb_lb_entry *current,
                       *previous = NULL;
    bool                count_ok,
                        order_ok,
                        span_ok;

    order_ok = true;
    RB_FOREACH(current, cb_lb_tree, &(lbset->head))
    {
        ++num_entries;

        if (previous &&
            cb_offset_cmp(previous->lower_bound, current->lower_bound) != -1)
        {
            order_ok = false;
        }

        previous = current;
    }

    count_ok = (num_entries == lbset->num_entries);

    span_ok = true;
    if (lbset->num_entries > 0) {
        cb_offset_t low  = RB_MIN(cb_lb_tree, &(lbset->head))->lower_bound;
        cb_offset_t high = RB_MAX(cb_lb_tree, &(lbset->head))->lower_bound;

        span_ok = (high - low < (CB_OFFSET_MAX / 2));
    }

    return count_ok && order_ok && span_ok;
}


int
cb_lb_set_init(struct cb_lb_set *lbset)
{
    lbset->num_entries = 0;
    lbset->lowest_bound = 0;
    RB_INIT(&(lbset->head));
    return 0;
}


int
cb_lb_set_add(struct cb_lb_set   *lbset,
              struct cb_lb_entry *lbentry)
{
    assert(RB_FIND(cb_lb_tree, &(lbset->head), lbentry) == NULL);
    RB_INSERT(cb_lb_tree, &(lbset->head), lbentry);
    ++(lbset->num_entries);
    if (lbset->num_entries == 1 ||
        cb_offset_cmp(lbentry->lower_bound, lbset->lowest_bound) == -1)
    {
        lbset->lowest_bound = lbentry->lower_bound;
    }

    heavy_assert(cb_lb_set_validate(lbset));

    cb_log_debug("%p added %p @ %ju -- {num_entries: %ju, lowest_bound: %ju}",
                 lbset,
                 lbentry,
                 (uintmax_t)lbentry->lower_bound,
                 (uintmax_t)lbset->num_entries,
                 (uintmax_t)lbset->lowest_bound);

    return 0;
}


int
cb_lb_set_remove(struct cb_lb_set   *lbset,
                 struct cb_lb_entry *lbentry)
{
    assert(RB_FIND(cb_lb_tree, &(lbset->head), lbentry) != NULL);
    RB_REMOVE(cb_lb_tree, &(lbset->head), lbentry);
    --(lbset->num_entries);
    if (lbentry->lower_bound == lbset->lowest_bound && lbset->num_entries > 0)
        lbset->lowest_bound = RB_MIN(cb_lb_tree, &(lbset->head))->lower_bound;

    heavy_assert(cb_lb_set_validate(lbset));

    cb_log_debug("%p removed %p @ %ju -- {num_entries: %ju, lowest_bound: %ju}",
                 lbset,
                 lbentry,
                 (uintmax_t)lbentry->lower_bound,
                 (uintmax_t)lbset->num_entries,
                 (uintmax_t)lbset->lowest_bound);

    return 0;
}


struct cb_lb_entry*
cb_lb_set_get_lowest_entry(struct cb_lb_set *lbset)
{
    struct cb_lb_entry *lowest_entry = NULL;

    if (lbset->num_entries > 0)
    {
        lowest_entry = RB_MIN(cb_lb_tree, &(lbset->head));
        assert(lbset->lowest_bound == lowest_entry->lower_bound);
    }

    return lowest_entry;
}

