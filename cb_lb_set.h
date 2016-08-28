#ifndef _CB_LB_SET_H_
#define _CB_LB_SET_H_

/*
 * The purpose of this module is a data structure which will provide fast
 * derivation of the lowest cb_offset_t among a set of entries.  This is
 * intended to be used for maintenance of knowledge of where the 'in-use' range
 * of a given continuous buffer begins. (A cursor internal to the continuous
 * buffer will track where the range ends.)  It is expected that the entries
 * of this set will represent green-threads whose backing store is the
 * continuous buffer to which this lower-bound set will correspond.
 *
 * The present implementation is based on a red-black tree, and so mutations
 * have an O(log n) cost, but we cache the lowest entry for O(1) derivation.
 *
 * This is anticipated to "just work" for cb_offset_t's which have the property
 * of being cyclical in nature, but I have no proof of this. (FIXME)
 *
 * Background
 * ----------
 * Using a "Hashed and Hierarchical Timing Wheel"-like structure here would
 * allow O(1) additions and removals and O(1) lookup of the lowest bound, but
 * there would seem to be a problem with this approach:
 * As progression of buckets within a level happens, the present level
 * eventually is exhausted and a "carry" of sorts happens where the next bucket
 * in the next higher level gets redistributed among buckets of all lower
 * levels.  The problem is that buckets of higher levels contain, in general,
 * a magnitude more entries than the buckets of lower levels (this is the
 * hierarchy).  This means that as reflow happens at higher levels, more of a
 * pause will happen as larger numbers of entries get redistributed.  An ideal
 * hierarchy would seem to be levels with 2 buckets each, where we have as many
 * levels as there are bits in a cb_offset_t (i.e. 64).  As the cb_offset_t
 * wraps around (from 0xFFFFFFFFFFFFFFFF to and through 0x0000000000000000)
 * there would need to be an overflow bucket to hold what essentially amounts
 * to *all* of the cb_offset_t's that have surpassed the overflow point.
 * Which means that as the overflow point is surpassed, all N entries will need
 * to suddenly be redistributed to the lower buckets.  This seems like it would
 * be a source of uneven behavior as our dataset progressed through the various
 * levels, with a maximum performance hit at this overflow point.  Such
 * redistribution costs are fine when such a data structure is implementing
 * timers because reflow can happen asynchronously to registered timers'
 * expirations via use of an independent internal timer which asynchronously
 * (to the registered timers) invokes redistributions.  However, for lower-
 * bound derivation purposes, such reflows must happen synchronously to the
 * derivation.  As my hopes are to make this framework suitable for realtime
 * applications (where we cannot rely on amortization, but instead must be
 * concerned about instantaneous performance), this approach is unsatisfactory
 * and a red-black tree is presently preferred.
 */

#include "external/tree.h"
#include "cb.h"

#ifdef __cplusplus
extern "C" {
#endif


struct cb_lb_entry
{
    cb_offset_t            lower_bound;
    RB_ENTRY(cb_lb_entry)  linkage;
};


RB_HEAD(cb_lb_tree, cb_lb_entry);


struct cb_lb_set
{
    uint64_t          num_entries;
    cb_offset_t       lowest_bound;
    struct cb_lb_tree head;
};


int
cb_lb_set_init(struct cb_lb_set *lbset);

int
cb_lb_set_add(struct cb_lb_set   *lbset,
              struct cb_lb_entry *lbentry);

int
cb_lb_set_remove(struct cb_lb_set   *lbset,
                 struct cb_lb_entry *lbentry);

struct cb_lb_entry*
cb_lb_set_get_lowest_entry(struct cb_lb_set *lbset);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _CB_LB_SET_H_ */
