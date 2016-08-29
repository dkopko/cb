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
#ifndef _CB_BITS_H_
#define _CB_BITS_H_

#include "cb.h"


#define BITS_PER_BYTE 8

#define likely(e)   __builtin_expect(!!(e), 1)
#define unlikely(e) __builtin_expect((e), 0)


/*
 * Returns a bitmask with a 1 in the position of each contiguous least-
 * significant bit which was zero in x. (e.g. 01100b -> 00011b)
 */
CB_INLINE size_t
contiguous_lsb_zeros_mask(size_t x)
{
    return (x ^ (x - 1)) >> 1;
}


/*
 * Returns the population count (number of ones).
 *
 * __builtin_popcount() only handles 'unsigned int' type (32 bits).  This is a
 * 64-bit popcount.
 */
CB_INLINE int
popcount64(uint64_t v)
{
    return __builtin_popcountll(v);
}


/*
 * Counts leading (MSB) zeros.  The value 0 is defined to return number of
 * bits of the data type (64).
 */
CB_INLINE int
clz64(uint64_t v)
{
    if (v == 0)
        return 64;

    return __builtin_clzll(v);
}


/*
 * Counts trailing (LSB) zeros.  The value 0 is defined to return number of
 * bits of the data type (64).
 */
CB_INLINE int
ctz64(uint64_t v)
{
    if (v == 0)
        return 64;

    return __builtin_ctzll(v);
}


/*
 * Counts leading (MSB) ones.
 */
CB_INLINE int
clo64(uint64_t v)
{
    return clz64(~v);
}


/*
 * Counts trailing (LSB) ones.
 */
CB_INLINE int
cto64(uint64_t v)
{
    return ctz64(~v);
}


/*
 * Retrieves 'count' bits from 'src' of the range ['pos', 'pos'+'count'].
 */
CB_INLINE uint64_t
bits_at(uint64_t src, uint64_t count, uint64_t pos)
{
    return (src & ((((uint64_t)1 << count) - 1) << pos)) >> pos;
}


/*
 * Returns whether 'x' is a power of 2.
 */
CB_INLINE bool
is_power_of_2(uintmax_t x)
{
    return (x > 0) & ((x ^ (x - 1)) == (x | (x - 1)));
}


/*
 * Returns whether the size_t 'x' is a power of 2.
 */
CB_INLINE bool
is_power_of_2_size(size_t x)
{
    return (x > 0) & ((x ^ (x - 1)) == (x | (x - 1)));
}


CB_INLINE uintmax_t
mask_below_bit(uint8_t x)
{
    if (x >= (sizeof(uintmax_t) * BITS_PER_BYTE))
        return (uintmax_t)-1;

    return ((uintmax_t)1 << x) - 1;
}


/*
 * Returns the lowest power of 2 size_t which is greater than 'x'.
 */
CB_INLINE size_t
power_of_2_size_gt(size_t x)
{
    size_t result = mask_below_bit(64 - clz64(x)) + 1;
    assert(is_power_of_2_size(result));
    return result;
}

/*
 * Returns the lowest power of 2 size_t which is greater than or equal to 'x'.
 */
CB_INLINE size_t
power_of_2_size_gte(size_t x)
{
    size_t result = (is_power_of_2_size(x) ? x : power_of_2_size_gt(x));
    assert(is_power_of_2_size(result));
    return result;
}


/*
 * For a size_t 'x' which is a power of two, returns floor(log2(x)).
 */
CB_INLINE unsigned int
log2_of_power_of_2_size(size_t x)
{
    assert(is_power_of_2_size(x));
    return 64 - clz64(x) - 1;
}


/*
 * Returns whether pointer 'p' is aligned to 'alignment'.
 */
CB_INLINE bool
is_ptr_aligned_to(const void *p, size_t alignment)
{
    assert(is_power_of_2_size(alignment));
    return ((uintptr_t)p & (alignment - 1)) == 0;
}


/*
 * Returns whether the size_t 'dividend' is evenly divisible by 'divisor'.
 */
CB_INLINE bool
is_size_divisible_by(size_t dividend, size_t divisor)
{
    assert(divisor != 0);
    return ((dividend / divisor) * divisor) == dividend;
}


/*
 * Returns a size_t which is strictly greater than 'min' and which is a multiple
 * of 'factor'.
 */
CB_INLINE size_t
size_multiple_gt(size_t min, size_t factor)
{
    assert(factor != 0);
    return ((min / factor) + 1) * factor;
}


/*
 * Returns a size_t which is greater than or equal to 'min' and which is a
 * multiple of 'factor'.
 */
CB_INLINE size_t
size_multiple_gte(size_t min, size_t factor)
{
    size_t gt = size_multiple_gt(min, factor);
    return (gt - factor == min) ? min : gt;
}


#endif /* _CB_BITS_H_ */
