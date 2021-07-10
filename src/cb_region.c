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

//FIXME Round size parameters up to a multiple of alignment? e.g.:
//      size = (size + alignment - 1) & ~alignment
//      This would allow a newly allocated region from cb_region_memalign()
//      which is adjacent/contiguous with the old one, to be unified with it.

#include "cb_region.h"

#include "cb.h"
#include "cb_assert.h"
#include "cb_bits.h"


static bool
cb_region_flags_validate(unsigned int flags)
{
    return (flags & CB_REGION_ALL_FLAGS) == flags;
}


bool
cb_region_validate(const struct cb_region *region)
{
    (void)region;
    cb_assert(cb_offset_lte(region->start, region->end));
    cb_assert(cb_offset_lte(region->start, region->cursor));
    cb_assert(cb_offset_lte(region->cursor, region->end));
    cb_assert(is_power_of_2_size(region->alignment));
    cb_assert(cb_region_flags_validate(region->flags));
    return true;
}


int
cb_region_create(struct cb        **cb,
                 struct cb_region  *region,
                 size_t             alignment,
                 size_t             size,
                 unsigned int       flags)
{
    cb_offset_t offset;
    int ret;

    alignment = power_of_2_gte_size(alignment);

    if (!cb_region_flags_validate(flags))
        return CB_BADPARAM;

    ret = cb_ensure_free_contiguous(cb, size + alignment - 1);
    if (ret != CB_SUCCESS)
        return ret;

    ret = cb_memalign(cb, &offset, alignment, size);
    if (ret != CB_SUCCESS)
        return ret;

    region->start          = offset;
    region->end            = offset + size;
    region->cursor         = offset + ((flags & CB_REGION_REVERSED) ? size : 0);
    region->preferred_size = size;
    region->alignment      = alignment;
    region->flags          = flags;

    cb_assert(cb_region_validate(region));
    return CB_SUCCESS;
}


int
cb_region_derive(struct cb_region *region,
                 struct cb_region *subregion,
                 size_t            alignment,
                 size_t            size,
                 unsigned int      flags)
{
    cb_offset_t subregion_start;
    cb_offset_t subregion_end;

    alignment = power_of_2_gte_size(alignment);

    if (!cb_region_flags_validate(flags))
        return CB_BADPARAM;

    if (region->flags & CB_REGION_REVERSED)
    {
        subregion_start =
            cb_offset_aligned_lte(region->cursor - size, alignment);
        subregion_end   = subregion_start + size;

        if (!cb_offset_lte(region->start, subregion_start))
            return CB_DEPLETED;

        region->cursor = subregion_start;
    }
    else
    {
        subregion_start = cb_offset_aligned_gte(region->cursor, alignment);
        subregion_end   = subregion_start + size;

        if (!cb_offset_lte(subregion_end, region->end))
            return CB_DEPLETED;

        region->cursor = subregion_end;
    }


    subregion->start          = subregion_start;
    subregion->end            = subregion_end;
    subregion->cursor         =
        ((flags & CB_REGION_REVERSED) ? subregion_end : subregion_start);
    subregion->preferred_size = size;
    subregion->alignment      = alignment;
    subregion->flags          = flags;

    cb_assert(cb_region_validate(region));
    return CB_SUCCESS;
}


static int
cb_region_memalign_final(struct cb_region  *region,
                         cb_offset_t       *offset,
                         size_t             alignment,
                         size_t             size)
{
    cb_offset_t mem_start;
    cb_offset_t mem_end;

    //printf("DANDEBUG cb_region_memalign_final(%p{s:%ju, e:%ju, c:%ju}, ..., a:%zd, s:%zd)\n",
    //       region, (uintmax_t)cb_region_start(region), (uintmax_t)cb_region_end(region), (uintmax_t)cb_region_cursor(region),
    //       (uintmax_t)alignment,
    //       (uintmax_t)size);

    alignment = power_of_2_gte_size(alignment);

    if (region->flags & CB_REGION_REVERSED)
    {
        mem_start = cb_offset_aligned_lte(region->cursor - size, alignment);
        mem_end   = mem_start + size;

        if (!cb_offset_lte(region->start, mem_start))
            return CB_DEPLETED;

        region->cursor = mem_start;
    }
    else
    {
        mem_start = cb_offset_aligned_gte(region->cursor, alignment);
        mem_end   = mem_start + size;

        if (!cb_offset_lte(mem_end, region->end))
            return CB_DEPLETED;

        //printf("DANDEBUG cb_region_memalign_final() derived %ju for mem_start. cursor %ju -> %ju\n",
        //       (uintmax_t)mem_start, (uintmax_t)region->cursor, (uintmax_t)mem_end);
        region->cursor = mem_end;
    }

    *offset = mem_start;

    cb_assert(cb_region_validate(region));
    return CB_SUCCESS;
}


int
cb_region_memalign(struct cb        **cb,
                   struct cb_region  *region,
                   cb_offset_t       *offset,
                   size_t             alignment,
                   size_t             size)
{
    size_t preferred_size;
    int ret;

    // Attempt to allocate in region's available area.
    ret = cb_region_memalign_final(region, offset, alignment, size);
    if (ret != CB_DEPLETED)
        return ret;

    // Final regions do not permit subsequent region allocations and must return
    // that they have been depleted.
    if (region->flags & CB_REGION_FINAL)
        return CB_DEPLETED;

    // No room in region for the attempted allocation, allocate a new region.
    // (In case we needed to allocate a specially-sized region, be sure to
    // retain the original region's preferred size.)
    preferred_size = region->preferred_size;
    ret = cb_region_create(cb,
                           region,
                           alignment,
                           (preferred_size > size ? preferred_size : size),
                           region->flags);
    if (ret != CB_SUCCESS)
        return ret;
    region->preferred_size = preferred_size;

    // Attempt to allocate in new region's available area.
    ret = cb_region_memalign_final(region, offset, alignment, size);
    cb_assert(ret == CB_SUCCESS);
    return ret;
}
