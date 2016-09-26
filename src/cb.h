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
#ifndef _CB_H_
#define _CB_H_

#include "cb_assert.h"
#include "cb_misc.h"

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


enum cb_log_level
{
    CB_LOG_DEBUG,
    CB_LOG_ERROR
};


void cb_log_impl(enum cb_log_level lvl, const char *fmt, ...);

#define cb_log(LVL, FMT, ARGS...) \
        cb_log_impl(LVL, "[cb|%s():%d] " FMT "\n", __FUNCTION__, __LINE__, \
                    ##ARGS)
#define cb_log_error(FMT, ARGS...) cb_log(CB_LOG_ERROR, FMT, ##ARGS)
#define cb_log_errno(FMT, ARGS...) cb_log(CB_LOG_ERROR, FMT " (\"%m\")", ##ARGS)
#ifdef CB_VERBOSE
#define cb_log_debug(FMT, ARGS...) cb_log(CB_LOG_DEBUG, FMT, ##ARGS)
#else
#define cb_log_debug(FMT, ARGS...) do { } while(0)
#endif


typedef uintptr_t cb_mask_t;
typedef uintptr_t cb_offset_t;
enum { CB_OFFSET_MAX = UINTPTR_MAX };

struct cb_key
{
    uint64_t k;
};


struct cb_value
{
    uint64_t v;
};


struct cb_params
{
    size_t        ring_size;
    size_t        loop_size;
    unsigned int  index;
    unsigned int  flags;
    int           open_flags;
    mode_t        open_mode;
    int           mmap_prot;
    int           mmap_flags;
    char          filename_prefix[64];
};

#define CB_PARAMS_F_LEAVE_FILES (1 << 0)


extern struct cb_params CB_PARAMS_DEFAULT;


struct cb
{
    size_t            page_size;
    size_t            header_size;
    size_t            loop_size;
    unsigned int      index;
    cb_mask_t         mask;
    cb_offset_t       data_start;
    cb_offset_t       cursor;
    struct cb        *link;
    char              filename[PATH_MAX];
    struct cb_params  params;
    cb_offset_t       last_command_offset;
    size_t            stat_wastage;
};


/*
 * Compares two offsets.  As offsets are cyclic, we say that 'rhs' is
 * "less-than" 'lhs' if it within one-half of the offset range below 'lhs', and
 * "greater-than" otherwise.
 */
CB_INLINE int
cb_offset_cmp(cb_offset_t lhs,
              cb_offset_t rhs)
{
    cb_offset_t diff = rhs - lhs;
    return diff == 0 ? 0 :
           diff < (CB_OFFSET_MAX / 2) ? -1 :
           1;
}


/*
 * Returns whether 'lhs' is "less-than-or-equal-to" 'rhs'.
 */
CB_INLINE bool
cb_offset_lte(cb_offset_t lhs,
              cb_offset_t rhs)
{
    return (rhs - lhs) < (CB_OFFSET_MAX / 2);
}


/*
 * Initializes the cb library/module.
 */
int
cb_module_init(void);


/*
 * Creates a continuous buffer based on the passed-in parameters.
 */
struct cb*
cb_create(struct cb_params *in_params,
          size_t            in_params_len);


/*
 * Destroys a continuous buffer.
 */
void
cb_destroy(struct cb *cb);

/*
 * Validates a continuous buffer.
 */
void
cb_validate2(const struct cb *cb);


/*
 * Copies memory out of the continuous buffer 'cb' and into 'dest'.  Memory
 * starts at the offset within cb of 'offset', and is of size 'len' bytes.
 *
 * This variant requires that 'len' is strictly less than the loop size.
 */
void
cb_memcpy_out_short(void            *dest,
                    const struct cb *cb,
                    cb_offset_t      offset,
                    size_t           len);


/*
 * Copies memory out of the continuous buffer 'cb' and into 'dest'.  Memory
 * starts at the offset within cb of 'offset', and is of size 'len' bytes.
 */
void
cb_memcpy_out(void            *dest,
              const struct cb *cb,
              cb_offset_t      offset,
              size_t           len);


/*
 * Copies memory from 'src' into the continuous buffer 'cb' at offset 'offset'.
 * Memory is of size 'len' bytes.
 *
 * This variant requires that 'len' is strictly less than the loop size.
 */
void
cb_memcpy_in_short(struct cb   *cb,
                   cb_offset_t  offset,
                   const void  *src,
                   size_t       len);


/*
 * Copies memory from 'src' into the continuous buffer 'cb' at offset 'offset'.
 * Memory is of size 'len' bytes.
 */
void
cb_memcpy_in(struct cb   *cb,
             cb_offset_t  offset,
             const void  *src,
             size_t       len);


/*
 * Copies memory from continuous buffer 'src_cb' starting at offset 'src_offset'
 * into continuous buffer 'dest_cb' at offset 'dest_offset'.  Memory is of size
 * 'len' bytes.
 */
void
cb_memcpy(struct cb       *dest_cb,
          cb_offset_t      dest_offset,
          const struct cb *src_cb,
          cb_offset_t      src_offset,
          size_t           len);


/*
 * Requests a resize (larger or smaller) of the continuous buffer 'cb' to the
 * requested size 'requested_ring_size'.
 */
int
cb_resize(struct cb **cb,
          size_t      requested_ring_size);


/*
 * Requests a resize larger of the continuous buffer 'cb' to the requested size
 * 'min_ring_size'.  The chosen size will be the next power of two greater than
 * 'min_ring_size'.
 */
int
cb_grow(struct cb **cb,
        size_t      min_ring_size);


/*
 * Requests a resize smaller of the continuous buffer 'cb' to the requested size
 * 'min_ring_size'.  The chosen size will be the next power of two greater than
 * 'min_ring_size'.
 */
int
cb_shrink(struct cb **cb,
          size_t      min_ring_size);


/*
 * Requests a resize smaller of the continuous buffer 'cb' to the smallest size
 * greater than its presently stored data range.
 */
int
cb_shrink_auto(struct cb **cb);


/*
 * Appends data at 'p' of size 'len' to the continuous buffer 'cb' at its
 * present cursor offset.  The cursor will be incremented by 'len' bytes.
 */
int
cb_append(struct cb **cb,
          void       *p,
          size_t      len);


/*
 * Allocates memory from the continuous buffer 'cb', whose location offset will
 * be returned in 'offset'.  The allocated memory will have alignment
 * 'alignment' and be of length 'size' bytes.
 */
int
cb_memalign(struct cb   **cb,
            cb_offset_t  *offset,
            size_t        alignment,
            size_t        size);


/*
 * Returns the size of the "ring" of the continuous buffer 'cb'.  The ring is
 * the presently exposed writeable area of the continuous buffer.  The ring has
 * a range which is a power of two, and it represents a range of offsets within
 * the cycle of offsets.
 */
CB_INLINE size_t
cb_ring_size(const struct cb *cb)
{
    return cb->mask + 1;
}


/*
 * Returns the loop size of the continuos buffer 'cb'.  The "loop" is a region
 * of the "ring" which is remapped in virtual memory to appear again immediately
 * subsequent to the end of the ring itself.  This creates a "magic ring buffer"
 * which allows writes off of the end of the ring to seamlessly produce writes
 * at the wrap-around beginning of the ring.  (This is handled by the
 * memory-management unit due to the virtual memory page remappings.)  Such
 * writes can proceed without program checking so long as their length is
 * less-than-or-equal-to the loop size.
 */
CB_INLINE size_t
cb_loop_size(const struct cb *cb)
{
    return cb->loop_size;
}


/*
 * Returns the raw pointer to the start of the ring of the continuous buffer
 * 'cb'.
 */
CB_INLINE void*
cb_ring_start(const struct cb *cb)
{
    return (char*)cb + cb->header_size;
}


/*
 * Returns the raw pointer to the end of the ring of the continuous buffer
 * 'cb'.  The end represents an address one byte past the writable ring area of
 * the continuous buffer, i.e. the range of the ring is [ring_start, ring_end).
 */
CB_INLINE void*
cb_ring_end(const struct cb *cb)
{
    return (char*)cb_ring_start(cb) + cb_ring_size(cb);
}


/*
 * Returns the raw pointer to the start of the loop of the continuous buffer
 * 'cb'.
 */
CB_INLINE void*
cb_loop_start(const struct cb *cb)
{
    return cb_ring_end(cb);
}


/*
 * Returns the raw pointer to the end of the loop of the continous buffer
 * 'cb'.  The end represents an address one byte past the writable loop area of
 * the continuous buffer, i.e. the range of the loop is [loop_start, loop_end).
 */
CB_INLINE void*
cb_loop_end(const struct cb *cb)
{
    return (char*)cb_ring_end(cb) + cb_loop_size(cb);
}


/*
 * Returns the size of the accumulated data held within the ring of the
 * continous buffer 'cb'.  This is represented by the range from where the
 * data began to be written up to the cursor.
 */
CB_INLINE size_t
cb_data_size(const struct cb *cb)
{
    return cb->cursor - cb->data_start;
}


/*
 * Returns the offset of the cursor within the continous buffer 'cb'.  Note that
 * this offset is an absolute offset within the cyclical range of offsets.  In
 * particular, the cursor may wrap around the ring of the continous buffer
 * several times without needing to resize the continous buffer (so long as the
 * low end of the range of data, 'data_start', also keeps progressing).
 */
CB_INLINE cb_offset_t
cb_cursor(const struct cb *cb)
{
    return cb->cursor;
}


/*
 * Rewinds the cursor of the continous buffer 'cb' to an earlier, lower offset.
 * This is suitable to use if an attempted action failed after allocating some
 * memory, in order to free the memory no longer needed.
 */
CB_INLINE void
cb_rewind_to(struct cb   *cb,
             cb_offset_t  offset)
{
    cb_assert(cb_offset_lte(offset, cb->cursor));
    cb->cursor = offset;
}


/*
 * Returns the free area within the ring of the contiuous buffer 'cb'.  This is
 * the size of the ring minus the size of the data already held within it.
 */
CB_INLINE size_t
cb_free_size(const struct cb *cb)
{
    return cb_ring_size(cb) - cb_data_size(cb);
}


/*
 * Returns a raw pointer to the area of memory presently holding the data of
 * continous buffer 'cb' at offset 'offset'.
 */
CB_INLINE void*
cb_at(const struct cb *cb,
      cb_offset_t      offset)
{
    /* offset >= data_start */
    cb_assert(cb_offset_cmp(offset, cb->data_start) > -1);

    /* offset <= data_end */
    cb_assert(cb_offset_cmp(offset, cb->data_start + cb_ring_size(cb)) < 1);

    return (char*)cb_ring_start(cb) + (offset & cb->mask);
}


#if 0
/*
 * Attempts to translate a raw pointer to an offset within the continous buffer
 * 'cb'.  However, there is ambiguity here, as a given raw address may
 * represent many offsets within the continous buffer, as the offsets wrap
 * around the ring to occupy the same raw memory locations as earlier offsets
 * (modulo the ring size).  The use of this function in general should be
 * avoided.
 */
CB_INLINE cb_offset_t
cb_from(const struct cb *cb,
        const void      *addr)
{
    cb_assert((char*)addr >= (char*)cb_ring_start(cb));
    cb_assert((char*)addr <= (char*)cb_ring_end(cb));

    return cb->data_start + ((char*)addr - (char*)cb_ring_start(cb));
}
#endif


/*
 * Ensures that there is at least 'len' bytes available to be written to in the
 * continous buffer 'cb', attempting to grow the continous buffer if needed.
 *
 * This variant does not ensure that the len bytes will be contiguous, only
 * that they will be free for writing.
 */
CB_INLINE int
cb_ensure_free(struct cb **cb,
               size_t      len)
{
    if (len <= cb_free_size(*cb))
        return 0;

    return cb_grow(cb, cb_data_size(*cb) + len);
}


/*
 * Ensures that the range of offsets up to, but not including, 'offset' is
 * available for writing in the continous buffer 'cb', attempting to grow the
 * continous buffer if needed.
 */
CB_INLINE int
cb_ensure_to(struct cb   **cb,
             cb_offset_t   offset)
{
    if (!cb_offset_lte((*cb)->cursor, offset))
        return -1;

    return cb_ensure_free(cb, offset - (*cb)->cursor);
}


CB_INLINE size_t
cb_contiguous_write_range(struct cb *cb)
{
    char *ring_start = (char*)cb_ring_start(cb);
    char *data_start = (char*)cb_at(cb, cb->data_start);
    char *cursor     = (char*)cb_at(cb, cb->cursor);
    char *ring_end   = (char*)cb_ring_end(cb);

    cb_assert(ring_start <= cursor);
    cb_assert(ring_start <= data_start);
    cb_assert(data_start < ring_end);
    cb_assert(cursor < ring_end);

    if (cursor >= data_start)
    {
        /*
         *                                       [-loop area-]
         * [---area 1---]              [--------area 2-------]
         * ..............DDDDDDDDDDDDDD..........LLLLLLLLLLLLL
         * ^             ^             ^         ^
         * ring_start    data_start    cursor    ring_end
         */

        size_t area1_len = (size_t)(data_start - ring_start);
        size_t area2_len = (size_t)(ring_end - cursor) +
            (area1_len < cb_loop_size(cb) ? area1_len : cb_loop_size(cb));

        return area2_len;
    }
    else
    {
        /*
         * DDDDDDDDDDDDDD..........DDDDDDDDDDDDDD
         * ^             ^         ^             ^
         * ring_start    cursor    data_start    ring_end
         */

        return (size_t)(data_start - cursor);
    }
}


/*
 * Ensures that the continuous buffer's cursor will be left pointing at an
 * offset which is contiguously writable for 'len' bytes, growing the continuous
 * buffer and/or advancing the cursor as necessary.
 */
CB_INLINE int
cb_ensure_free_contiguous(struct cb **cb,
                          size_t      len)
{
    char *ring_start = (char*)cb_ring_start(*cb);
    char *data_start = (char*)cb_at(*cb, (*cb)->data_start);
    char *cursor     = (char*)cb_at(*cb, (*cb)->cursor);
    char *ring_end   = (char*)cb_ring_end(*cb);

    cb_assert(ring_start <= cursor);
    cb_assert(ring_start <= data_start);
    cb_assert(data_start < ring_end);
    cb_assert(cursor < ring_end);

    if (cursor >= data_start)
    {
        /*
         *                                       [-loop area-]
         * [---area 1---]              [--------area 2-------]
         * ..............DDDDDDDDDDDDDD..........LLLLLLLLLLLLL
         * ^             ^             ^         ^
         * ring_start    data_start    cursor    ring_end
         */

        size_t area1_len = (size_t)(data_start - ring_start);
        size_t area2_len = (size_t)(ring_end - cursor) +
            (area1_len < cb_loop_size(*cb) ? area1_len : cb_loop_size(*cb));

        if (len < area2_len)
            return 0;

        if (len < area1_len)
        {
            (*cb)->cursor += (ring_end - cursor);
            return 0;
        }
    }
    else
    {
        /*
         * DDDDDDDDDDDDDD..........DDDDDDDDDDDDDD
         * ^             ^         ^             ^
         * ring_start    cursor    data_start    ring_end
         */

        if ((size_t)(data_start - cursor) < len)
            return 0;
    }

    return cb_grow(cb, cb_ring_size(*cb) + len);
}

#endif /* ! defined _CB_H_*/
