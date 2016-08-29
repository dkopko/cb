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
#include "cb_bits.h"

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
TODO:
3) Complete memory functions:
    cb_calloc()
    cb_malloc()
    cb_memset()
    cb_realloc() ?
    cb_memmove()
    cb_memcpy*() - Determine if these have same semantic in regards to restrict
                   as the original memcpy().
5) extend the BST to become an RB-tree.
6) HAMT structures and operations.
7) Enumeration of allocations with lookup through HAMT, to allow for GC
   relocations of original allocation.
8) Performance measurement of direct pointer access vs dereference through an
   allocation number. (*ptr vs. cb_at(hamt_lookup(alloc_index)))
9) RB-tree join() operation.
10) RB-tree delete() operation.
11) Stacks
   * It seems that in order for prior frames to be GC'd (relocated), that the
     parent frame reference needs to be an alloc_index instead of a cb_offset_t.
   * It's not clear if stack frames are writeable by their owning process once
     they have been shifted to the GC region, or if they'll need to be copied
     out.
   * It seems that stack frames must have some form of reflection/introspection
    to allow for pointer/cb_offset_t rewriting in contained cells.
12) Describe the policy of HAMTs and RB-trees within the live region and the GC
    region.
13) Improve cb_print() output to show absolute and relative offsets between
    commands.
14) Keep track of garbage after cb_consolidate.

*/

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


#define cb_alignof(x) (__alignof__(x))

static size_t cb_page_size;
static const unsigned int CB_MAX_MMAP_RETRIES = 5;


struct cb_params CB_PARAMS_DEFAULT =
    {
        .ring_size = 0,
        .loop_size = 0,
        .index = 0,
        .open_flags = O_RDWR | O_CREAT | O_TRUNC,
        .open_mode = S_IRUSR | S_IWUSR,
        .mmap_prot = PROT_READ | PROT_WRITE,
        .mmap_flags = MAP_SHARED | MAP_ANONYMOUS,
        .filename_prefix = "map"
    };


CB_INLINE cb_offset_t offset_aligned_gte(cb_offset_t start, size_t alignment)
{
    assert(is_power_of_2_size(alignment));
    return ((start - 1) | (alignment - 1)) + 1;
}


CB_INLINE void cb_validate(const struct cb *cb)
{
    assert(cb->page_size != 0);
    assert(is_power_of_2_size(cb->page_size));

    assert(is_ptr_aligned_to(cb, cb->page_size));

    assert(cb->header_size >= sizeof(struct cb));
    assert(is_size_divisible_by(cb->header_size, cb->page_size));

    assert(cb->loop_size >= cb->page_size);
    assert(is_size_divisible_by(cb->loop_size, cb->page_size));

    assert(is_power_of_2(cb->mask + 1));

    assert(cb_offset_lte(cb->data_start, cb->cursor));

    assert(is_ptr_aligned_to(cb->link, cb->page_size));

    assert(cb->mask + 1 == cb->params.ring_size);
    assert(is_power_of_2_size(cb->params.ring_size));
}

/*FIXME consolidate with cb_validate(), in a way usable from cb_map. */
void cb_validate2(const struct cb *cb)
{
    cb_validate(cb);
}

/* Alternatives with linkage for these inline functions */
extern inline int cb_offset_cmp(cb_offset_t lhs, cb_offset_t rhs);
extern inline bool cb_offset_lte(cb_offset_t lhs, cb_offset_t rhs);


//FIXME log threadid
void cb_log_impl(enum cb_log_level lvl, const char *fmt, ...)
{
    int old_errno;
    FILE *file;
    va_list args;

    old_errno = errno;

    file = (lvl == CB_LOG_ERROR ? stderr : stdout);
    va_start(args, fmt);
    vfprintf(file, fmt, args);
    va_end(args);

    errno = old_errno;
}


static size_t ring_size_gte(size_t min_ring_size, size_t page_size)
{
    size_t ring_size;

    assert(page_size > 0);
    assert(is_power_of_2_size(page_size));

    ring_size = min_ring_size;

    if (ring_size == 0)
    {
        cb_log_debug("0 defaults to page size (%zu).", page_size);
        ring_size = page_size;
    }

    if (!is_size_divisible_by(ring_size, page_size))
    {
        size_t new_ring_size = size_multiple_gt(ring_size, page_size);
        cb_log_debug("%zu not divisible by page size %zu, increasing"
                     " to %zu.",
                     ring_size, page_size, new_ring_size);
        ring_size = new_ring_size;
    }

    if (!is_power_of_2_size(ring_size))
    {
        size_t new_ring_size = power_of_2_size_gt(ring_size);
        cb_log_debug("%zu not a power of 2, increasing to %zu.",
                     ring_size, new_ring_size);
        ring_size = new_ring_size;
    }

    assert(is_size_divisible_by(ring_size, page_size));
    assert(is_power_of_2_size(ring_size));

    return ring_size;
}


int cb_module_init(void)
{
    long ret;

#if defined _SC_PAGESIZE
    ret = sysconf(_SC_PAGESIZE);
    if (ret >= 0)
    {
        assert(is_power_of_2(ret));
        cb_page_size = (size_t)ret;
        return 0;
    }
    assert(ret == -1);
    cb_log_errno("sysconf(_SC_PAGESIZE) failed.");
#endif /* defined _SC_PAGESIZE */

    return -1;
}


struct cb* cb_create(struct cb_params *in_params, size_t in_params_len)
{
    struct cb *cb = NULL;
    struct cb_params params = CB_PARAMS_DEFAULT;
    size_t page_size, header_size;
    int fd = -1;
    unsigned int num_mmap_retries = 0;
    void *loop_addr;
    void *mem = MAP_FAILED, *loopmem = MAP_FAILED;
    char map_name[sizeof(cb->filename)];
    int ret;

    /* Copy parameters. */
    if (in_params)
    {
        if (in_params_len > sizeof(params))
        {
            cb_log_debug("only using first %zu (out of %zu supplied) bytes of"
                         " params.",
                         sizeof(params), in_params_len);
            in_params_len = sizeof(params);
        }

        memcpy(&params, in_params, in_params_len);
    }

    /* Determine page_size. */
    page_size = cb_page_size;
    if (page_size == 0)
    {
        cb_log_error("call cb_module_init() first.");
        return NULL;
    }
    assert(is_power_of_2_size(cb_page_size));

    /* Normalize the ring_size to something adequate. */
    params.ring_size = ring_size_gte(params.ring_size, page_size);
    assert(is_size_divisible_by(params.ring_size, page_size));
    assert(is_power_of_2_size(params.ring_size));

    /* Determine header_size. */
    header_size = size_multiple_gte(sizeof(struct cb), page_size);

    /* Determine loop_size. */
    if (params.loop_size == 0)
    {
        cb_log_debug("loop size defaulted to page size (%zu).", page_size);
        params.loop_size = page_size;

    }
    if (!is_size_divisible_by(params.loop_size, page_size))
    {
        size_t new_loop_size = size_multiple_gt(params.loop_size, page_size);
        cb_log_debug("loop size %zu not divisible by page size %zu, increasing"
                     " to %zu.",
                     params.loop_size, page_size, new_loop_size);
        params.loop_size = new_loop_size;
    }
    assert(is_size_divisible_by(params.loop_size, page_size));

    /* Back map by file if requested. */
    if (!(params.mmap_flags & MAP_ANONYMOUS))
    {
        ret = snprintf(map_name, sizeof(map_name), "%s%s%u-%u",
                       params.filename_prefix,
                       (params.filename_prefix[0] != '\0' ? "-" : ""),
                       params.index,
                       log2_of_power_of_2_size(params.ring_size));
        if (ret == -1) {
            cb_log_errno("snprintf() failed.");
            return NULL;
        }
        if ((size_t)ret > sizeof(map_name) - 1) {
            cb_log_error("snprintf() output truncated.");
            return NULL;
        }

        fd = open(map_name, params.open_flags, params.open_mode);
        if (fd == -1)
        {
            cb_log_errno("open(\"%s\") failed.", map_name);
            return NULL;
        }
        cb_log_debug("open(\"%s\") succeeded. (fd: %d)", map_name, fd);

        /*
         * Allocate size for the struct cb header and the ring, but not
         * the loop pages as those come from the ring.
         */
        ret = ftruncate(fd, header_size + params.ring_size);
        if (ret == -1)
        {
            cb_log_errno("ftruncate(%d, %zu) failed.",
                         fd, header_size + params.ring_size);
            goto fail;
        }
        cb_log_debug("ftruncate(%d, %zu) succeeded.",
                     fd, header_size + params.ring_size);
    }
    else
    {
        map_name[0] = '\0';
        fd = -1;
    }

    /*
     * Allocate contiguous address space:
     *   1) the struct cb header (page-aligned),
     *   2) the ring (page-aligned),
     *   3) loop pages to be remapped (page-aligned).
     */
mmap_retry:
    if (mem != MAP_FAILED)
    {
        assert(num_mmap_retries > 0);
        ret = munmap(mem, header_size + params.ring_size);
        if (ret == -1)
        {
            cb_log_errno("munmap() (retry) failed.");
            goto fail;
        }
    }
    mem = mmap(NULL,
               header_size + params.ring_size + params.loop_size,
               params.mmap_prot,
               params.mmap_flags,
               fd,
               0);
    if (mem == MAP_FAILED)
    {
        cb_log_errno("mmap() failed.");
        goto fail;
    }
    cb_log_debug("mmap() succeeded. (mem: %p)", mem);
    loop_addr = (char*)mem + header_size + params.ring_size;

    ret = munmap(loop_addr, params.loop_size);
    if (ret == -1)
    {
        cb_log_errno("munmap(%p, %zu) failed.", loop_addr, params.loop_size);
        goto fail;
    }
    cb_log_debug("munmap(%p, %zu) succeeded.", loop_addr, params.loop_size);

    loopmem = mmap(loop_addr,
                   params.loop_size,
                   params.mmap_prot,
                   params.mmap_flags,
                   fd,
                   header_size);
    if (loopmem == MAP_FAILED)
    {
        cb_log_errno("mmap() (loop) failed.");

        if (++num_mmap_retries < CB_MAX_MMAP_RETRIES)
            goto mmap_retry;

        goto fail;
    }
    if (loopmem != loop_addr)
    {
        cb_log_error("mmap() (loop) failed to obey address hint.");

        ret = munmap(loopmem, params.loop_size);
        if (ret == -1)
        {
            cb_log_errno("munmap() (loop) failed.");
            goto fail;
        }

        if (++num_mmap_retries < CB_MAX_MMAP_RETRIES)
            goto mmap_retry;

        goto fail;
    }
    cb_log_debug("mmap() (loop) succeeded. (loopmem: %p)", loopmem);
    assert(loopmem == loop_addr);

    /* Close unneeded fd, if created in backed-by-file case. */
    if (fd != -1)
    {
        do
        {
            ret = close(fd);
            if (ret == -1)
                cb_log_errno("close(%d) failed.", fd);
            else
                cb_log_debug("close(%d) succeeded.", fd);
        } while (ret == -1 && errno == EINTR);
    }

    /* Assign cb and contents. */
    cb = mem;
    cb->page_size = page_size;
    cb->header_size = header_size;
    cb->loop_size = params.loop_size;
    cb->index = params.index;
    cb->mask = params.ring_size - 1;
    cb->data_start = 0;
    cb->cursor = 0;
    cb->link = NULL;
    strncpy(cb->filename, map_name, sizeof(cb->filename));
    if (cb->filename[sizeof(cb->filename) - 1] != '\0')
    {
        cb_log_error("generated filename too long.");
        goto fail;
    }
    memcpy(&(cb->params), &params, sizeof(cb->params));
    cb->last_command_offset = 0;
    cb->stat_wastage = 0;
    assert(loopmem == cb_ring_end(cb));

    return cb;

fail:
    if (map_name[0] != '\0')
    {
        ret = unlink(map_name);
        if (ret == -1)
            cb_log_errno("unlink(%s) failed.", map_name);
        else
            cb_log_debug("unlink(%s) succeeded.", map_name);
    }

    if (fd != -1)
    {
        do
        {
            ret = close(fd);
            if (ret == -1)
                cb_log_errno("close(%d) failed.", fd);
            else
                cb_log_debug("close(%d) failed.", fd);
        } while (ret == -1 && errno == EINTR);
    }

    if (mem != MAP_FAILED)
    {
        size_t munmap_size = header_size + params.ring_size + params.loop_size;

        ret = munmap(mem, munmap_size);
        if (ret == -1)
            cb_log_errno("munmap(%p, %zu) failed.", mem, munmap_size);
        else
            cb_log_debug("munmap(%p, %zu) succeeded.", mem, munmap_size);
    }

    return NULL;
}


void cb_destroy(struct cb *cb)
{
    int ret;

    cb_validate(cb);

    if (cb->filename[0] != '\0' &&
        !(cb->params.flags & CB_PARAMS_F_LEAVE_FILES))
    {
        ret = unlink(cb->filename);
        if (ret == -1)
            cb_log_errno("unlink(%s) failed.", cb->filename);
    }

    ret = munmap(cb, cb->header_size + cb_ring_size(cb) + cb_loop_size(cb));
    if (ret == -1)
        cb_log_errno("munmap() failed.");
}


void cb_memcpy_out_short(void *dest,
                         const struct cb *cb, cb_offset_t offset,
                         size_t len)
{
    /* Simple write, must be contiguous due to loop pages. */
    assert(len < cb_loop_size(cb));
    memcpy(dest, cb_at(cb, offset), len);
}


void cb_memcpy_out(void *dest,
                   const struct cb *cb, cb_offset_t offset,
                   size_t len)
{
    void *src_start, *src_end, *ring_start, *ring_end;
    size_t upper_frag_len, lower_frag_len;

    cb_validate(cb);

    /* Error to wrap-around copy more data than is stored. */
    assert(len <= cb_ring_size(cb));
    //FIXME logerr?

    if (len < cb_loop_size(cb))
    {
        cb_memcpy_out_short(dest, cb, offset, len);
        return;
    }

    src_start = cb_at(cb, offset);
    src_end   = cb_at(cb, offset + len);

    if (src_start < src_end)
    {
        /* Simple write, contiguous. */
        memcpy(dest, src_start, len);
        return;
    }

    /* Discontiguous write, write in two pieces. */
    ring_start = cb_ring_start(cb);
    ring_end   = cb_ring_end(cb);
    upper_frag_len = ring_end - src_start;
    lower_frag_len = len - upper_frag_len;

    memcpy(dest, src_start, upper_frag_len);
    memcpy((char*)dest + upper_frag_len, ring_start, lower_frag_len);
}


void cb_memcpy_in_short(struct cb *cb, cb_offset_t offset,
                        const void *src,
                        size_t len)
{
    /* Simple write, must be contiguous due to loop pages. */
    assert(len < cb_loop_size(cb));
    memcpy(cb_at(cb, offset), src, len);
}


void cb_memcpy_in(struct cb *cb, cb_offset_t offset,
                  const void *src,
                  size_t len)
{
    void *dest_start, *dest_end, *ring_start, *ring_end;
    size_t upper_frag_len, lower_frag_len;

    cb_validate(cb);

    /*
     * This function shouldn't resize, nor should it be expected to handle
     * wrap-around writes which overlap with themselves.
     */
    assert(len <= cb_ring_size(cb));

    if (len < cb_loop_size(cb))
    {
        cb_memcpy_in_short(cb, offset, src, len);
        return;
    }

    dest_start = cb_at(cb, offset);
    dest_end   = cb_at(cb, offset + len);

    if (dest_start < dest_end)
    {
        /* Simple write, contiguous. */
        memcpy(dest_start, src, len);
        return;
    }

    /* Discontiguous write, write in two pieces. */
    ring_start = cb_ring_start(cb);
    ring_end   = cb_ring_end(cb);
    upper_frag_len = ring_end - dest_start;
    lower_frag_len = len - upper_frag_len;

    memcpy(dest_start, src, upper_frag_len);
    memcpy(ring_start, (char*)src + upper_frag_len, lower_frag_len);
}


void cb_memcpy(struct cb *dest_cb, cb_offset_t dest_offset,
               const struct cb *src_cb, cb_offset_t src_offset,
               size_t len)
{
    void *src_start, *src_end, *dest_start, *dest_end;
    size_t dest_upper_frag_len, src_upper_frag_len;
    size_t frag[2];

    cb_validate(src_cb);
    cb_validate(dest_cb);
    assert(len <= cb_ring_size(src_cb));
    assert(len <= cb_ring_size(dest_cb));

    src_start  = cb_at(src_cb, src_offset);
    dest_start = cb_at(dest_cb, dest_offset);
    (void)src_end;  /* only used in assert */
    (void)dest_end; /* only used in assert */

    src_upper_frag_len  = (char*)cb_loop_end(src_cb) - (char*)src_start;
    dest_upper_frag_len = (char*)cb_loop_end(dest_cb) - (char*)dest_start;

    assert((src_end = cb_at(src_cb, src_offset + len),
            len <= src_upper_frag_len + cb_loop_size(src_cb) ||
            src_start > src_end ||
            (src_start == src_end && src_start != cb_ring_start(src_cb))));
    assert((dest_end = cb_at(dest_cb, dest_offset + len),
            len <= dest_upper_frag_len + cb_loop_size(src_cb) ||
            dest_start > dest_end ||
            (dest_start == dest_end && dest_start != cb_ring_start(dest_cb))));

    /*
     * This section is essentially an unrolled sort of a three-element frag
     * array containing the elements [len, src_upper_frag_len,
     * dest_upper_frag_len], with a clip of all elements to no more than len.
     * (The non-existent third element, frag[2], is being represented by len
     * because with clipping all paths lead to frag[2] holding value len.)
     */
    if (len <= dest_upper_frag_len)
    {
        frag[0] = (src_upper_frag_len < len) ? src_upper_frag_len : len;
        frag[1] = len;
    }
    else
    {
        if (src_upper_frag_len <= dest_upper_frag_len)
        {
            frag[0] = src_upper_frag_len;
            frag[1] = dest_upper_frag_len;
        }
        else
        {
            frag[0] = dest_upper_frag_len;
            frag[1] = (src_upper_frag_len < len) ? src_upper_frag_len : len;
        }
    }

    memcpy(dest_start, src_start, frag[0]);
    memcpy(cb_at(dest_cb, dest_offset + frag[0]),
           cb_at(src_cb, src_offset + frag[0]),
           frag[1] - frag[0]);
    memcpy(cb_at(dest_cb, dest_offset + frag[1]),
           cb_at(src_cb, src_offset + frag[1]),
           len - frag[1]);
}


int cb_resize(struct cb **cb, size_t requested_ring_size)
{
    struct cb_params new_params;
    struct cb *new_cb;

    cb_validate(*cb);

    if (requested_ring_size == 0 ||
        !is_size_divisible_by(requested_ring_size, (*cb)->page_size))
    {
        cb_log_error("requested_ring_size (%zu) is not a positive multiple of"
                     "source page size (%zu).",
                     requested_ring_size,
                     (*cb)->page_size);
        return -1;
    }

    if (!is_power_of_2_size(requested_ring_size))
    {
        cb_log_error("requested_ring_size (%zu) is not a power of 2.",
                     requested_ring_size);
        return -1;
    }

    if (requested_ring_size < cb_data_size(*cb))
    {
        cb_log_error("requested_ring_size smaller than data size.");
        return -1;
    }

    if (requested_ring_size == cb_ring_size(*cb))
    {
        cb_log_debug("requested_ring_size equals existing ring_size.");
        return -1;
    }

    cb_log_debug("%s to %zu",
                 (requested_ring_size < cb_ring_size(*cb) ? "shrink" : "grow"),
                 requested_ring_size);

    /* Allocate a new cb with the larger requested ring size. */
    memcpy(&new_params, &(*cb)->params, sizeof(new_params));
    new_params.ring_size = requested_ring_size;
    new_params.index = (*cb)->index + 1;
    new_cb = cb_create(&new_params, sizeof(new_params));
    if (!new_cb)
    {
        cb_log_error("failed to create new cb");
        return -1;
    }

    /* Copy the contents of the old cb's ring into the new cb's ring. */
    new_cb->data_start = (*cb)->data_start;
    new_cb->cursor     = (*cb)->cursor;
    new_cb->link       = *cb;
    cb_memcpy(new_cb, (*cb)->data_start,
              *cb, (*cb)->data_start,
              cb_data_size(*cb));

    *cb = new_cb;

    return 0;
}


int cb_grow(struct cb **cb, size_t min_ring_size)
{
    size_t request_ring_size;

    cb_validate(*cb);

    request_ring_size = ring_size_gte(min_ring_size, (*cb)->page_size);

    if (request_ring_size < cb_ring_size(*cb))
    {
        cb_log_error("request ring size %zu (derived from specified minimum"
                     " of %zu) < existing ring size (%zu).",
                     request_ring_size, min_ring_size, cb_ring_size(*cb));
        return -1;
    }

    if (request_ring_size == cb_ring_size(*cb))
    {
        cb_log_debug("request ring size %zu (derived from specified minimum"
                     " of %zu) == existing ring size (%zu).",
                     request_ring_size, min_ring_size, cb_ring_size(*cb));
        return 0;
    }

    return cb_resize(cb, request_ring_size);
}


int cb_shrink(struct cb **cb, size_t min_ring_size)
{
    size_t request_ring_size;

    cb_validate(*cb);

    request_ring_size = ring_size_gte(min_ring_size, (*cb)->page_size);

    if (request_ring_size > cb_ring_size(*cb))
    {
        cb_log_error("request ring size %zu (derived from specified minimum"
                     " of %zu) > existing ring size (%zu).",
                     request_ring_size, min_ring_size, cb_ring_size(*cb));
        return -1;
    }

    if (request_ring_size == cb_ring_size(*cb))
    {
        cb_log_debug("request ring size %zu (derived from specified minimum"
                     " of %zu) == existing ring size (%zu).",
                     request_ring_size, min_ring_size, cb_ring_size(*cb));
        return 0;
    }

    return cb_resize(cb, request_ring_size);
}


int cb_shrink_auto(struct cb **cb)
{
    return cb_shrink(cb, cb_data_size(*cb));
}


int cb_append(struct cb **cb, void *p, size_t len)
{
    int ret;

    cb_validate(*cb);

    ret = cb_ensure_free(cb, len);
    if (ret != 0)
        return ret;

    cb_memcpy_in(*cb, (*cb)->cursor, p, len);
    (*cb)->cursor += len;
    return 0;
}


int cb_memalign(struct cb **cb,
                cb_offset_t *offset,
                size_t alignment,
                size_t size)
{
    /*
     * NOTE: Allocations of size <= loop_size are guaranteed to be contiguous.
     * Allocations of size > loop_size are not guaranteed to be contiguous and
     * therefore should be treated as if they may be fragmented. This is due to
     * the fact that at any time in the future a cb_shrink() could make a
     * formerly contiguous region of size > loop_size become discontiguous.
     * Therefore, we don't make any attempts to make this region contiguous now,
     * as it cannot be held in perpetuity.
     * Example:
     * The ring size is 8 pages (0...7). The returned region offset starts at
     * start of page 3 and takes up 4 contiguous pages (3,4,5,6).  Later,
     * cb_shrink() is called, shrinking the ring size down to 4 pages.  New
     * addressing layout of the region's memory pages looks like (4,5,6,3)[4]
     * (where the [4] indicates a loop page repeating the start of the ring).
     */

    cb_offset_t start_offset;
    int ret;

    if (!is_power_of_2_size(alignment))
        return -1;

    start_offset = offset_aligned_gte((*cb)->cursor, alignment);

    ret = cb_ensure_to(cb, start_offset + size);
    if (ret != 0)
        return ret;

    (*cb)->cursor += start_offset - (*cb)->cursor + size;
    (*cb)->stat_wastage += start_offset - (*cb)->cursor;

    *offset = start_offset;
    return 0;
}

