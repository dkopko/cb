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
#ifndef _CB_REGION_H_
#define _CB_REGION_H_

#include "cb.h"

#ifdef __cplusplus
extern "C" {
#endif

//Region purposes:
// 1) Pre-allocate a section of the CB of a known size, so as to allow the GC
//    to consolidate other memory within the same CB into this region as the
//    thread which owns this CB continues to write to it (in areas other than
//    this set-aside region).  Regions used for this purpose should never be
//    extended, as they should be exactly sized for the consolidation and it
//    would be unsafe or inefficient to simultaneously adjust the cursor from
//    both the GC thread and the thread owning the CB.
// 2) To subdivide exposure of the visibility of a series of allcations.
//    Instead of the single cursor of the thread's CB indicating a thread-wide
//    cutoff offset across all green-threads, each independent green-thread can
//    write to its own series of regions, with each green-thread then able to
//    have an independent cutoff offset (its present region's cursor)
//    relative to its series of regions.
//        Regions used for this purpose should automatically allocate subsequent
//    regions from the thread's CB when they are depleted.  These regions should
//    be sized and should allocate replacements with sizing such as to not waste
//    space in the CB through fragmentation.
//
// Region considerations:
// Q: Should the sizing strategy of automatic allocation of regions themselves
//    (that which happen when regions are depleted) get driven by a parameter at
//    the time of the allocation attempt, or by attribute(s) of the region?
// A: The strategy of automatic allocations of regions needs to be driven by
//    attribute(s) of the region for separation-of-concerns issues.  Otherwise,
//    the data structures which allocate from regions would need to have
//    explicit awareness and parameters for different region reallocation
//    strategies, to which they should ideally have remained agnostic.


enum cb_region_flags
{
    CB_REGION_REVERSED = (1 << 0),
    CB_REGION_FINAL    = (1 << 1),
    CB_REGION_ALL_FLAGS = (CB_REGION_REVERSED | CB_REGION_FINAL)
};


/*
 * For forward allocations, next allocation offset will >= cursor.
 * For reverse allocations, next allocation offset will < cursor.
 */
struct cb_region
{
    cb_offset_t  start;
    cb_offset_t  end;
    cb_offset_t  cursor;
    size_t       preferred_size;
    size_t       alignment;
    unsigned int flags;
};


/*
 * Checks that a cb_region's fields are not obviously misconfigured.
 */
bool
cb_region_validate(const struct cb_region *region);


/*
 * Creates a new region within a continuous buffer, extending the continuous
 * buffer if necessary.
 */
int
cb_region_create(struct cb        **cb,
                 struct cb_region  *region,
                 size_t             alignment,
                 size_t             size,
                 unsigned int       flags);


/*
 * Derives a subregion from a region.
 */
int
cb_region_derive(struct cb_region *region,
                 struct cb_region *subregion,
                 size_t            alignment,
                 size_t            size,
                 unsigned int      flags);


/*
 * Allocates a piece of memory from a region.  This differs from a subregion,
 * in that only an offset is returned with no associated range information. If
 * there is insufficient space within the passed-in region for the attempted
 * allocation, and if the provided region is not marked with the CB_REGION_FINAL
 * flag, this will extend the region or allocate a new one.
 */
int
cb_region_memalign(struct cb        **cb,
                   struct cb_region  *region,
                   cb_offset_t       *offset,
                   size_t             alignment,
                   size_t             size);

CB_INLINE cb_offset_t
cb_region_start(const struct cb_region *region)
{
    return region->start;
}


CB_INLINE cb_offset_t
cb_region_end(const struct cb_region *region)
{
    return region->end;
}


CB_INLINE cb_offset_t
cb_region_cursor(const struct cb_region *region)
{
    return region->cursor;
}


CB_INLINE size_t
cb_region_preferred_size(const struct cb_region *region)
{
    return region->preferred_size;
}


CB_INLINE size_t
cb_region_alignment(const struct cb_region *region)
{
    return region->alignment;
}


CB_INLINE unsigned int
cb_region_flags(const struct cb_region *region)
{
    return region->flags;
}


CB_INLINE size_t
cb_region_size(const struct cb_region *region)
{
    return region->end - region->start;
}


CB_INLINE size_t
cb_region_remaining(const struct cb_region *region)
{
    return ((region->flags & CB_REGION_REVERSED) ?
            region->cursor - region->start : region->end - region->cursor);
}

CB_INLINE int
cb_region_ensure_free_contiguous(struct cb        **cb,
                                 struct cb_region  *region,
                                 size_t             len)
{
    cb_offset_t new_cursor;
    int ret;

    ret = cb_region_memalign(cb, region, &new_cursor, 1, len);
    if (ret != CB_SUCCESS) {
        return ret;
    }

    region->cursor = new_cursor;
    return CB_SUCCESS;
}

CB_INLINE int
cb_region_align_cursor(struct cb        **cb,
                       struct cb_region  *region,
                       size_t             alignment)
{
    cb_offset_t offset;
    int ret;

    ret = cb_region_memalign(cb, region, &offset, alignment, 1);
    if (ret != CB_SUCCESS)
        return ret;

    region->cursor = offset;
    return CB_SUCCESS;
}


#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* ! defined _CB_REGION_H_*/
