#ifndef _CB_H_
#define _CB_H_

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


#define CB_INLINE inline __attribute__((always_inline))
#define cb_alignof(x) (__alignof__(x))

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

#ifdef CB_HEAVY_ASSERT
#define heavy_assert assert
#else
#define heavy_assert(X) do { } while(0)
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


CB_INLINE int cb_offset_cmp(cb_offset_t lhs, cb_offset_t rhs)
{
    cb_offset_t diff = rhs - lhs;
    return diff == 0 ? 0 :
           diff < (CB_OFFSET_MAX / 2) ? -1 :
           1;
}


CB_INLINE bool cb_offset_lte(cb_offset_t lhs, cb_offset_t rhs)
{
    return (rhs - lhs) < (CB_OFFSET_MAX / 2);
}


int cb_module_init(void);

struct cb* cb_create(struct cb_params *in_params, size_t in_params_len);
void cb_destroy(struct cb *cb);

void cb_validate2(const struct cb *cb);

void cb_memcpy_out_short(void *dest,
                         const struct cb *cb, cb_offset_t offset,
                         size_t len);
void cb_memcpy_out(void *dest,
                   const struct cb *cb, cb_offset_t offset,
                   size_t len);
void cb_memcpy_in_short(struct cb *cb, cb_offset_t offset,
                        const void *src,
                        size_t len);
void cb_memcpy_in(struct cb *cb, cb_offset_t offset,
                  const void *src,
                  size_t len);
void cb_memcpy(struct cb *dest_cb, cb_offset_t dest_offset,
               const struct cb *src_cb, cb_offset_t src_offset,
               size_t len);

int cb_resize(struct cb **cb, size_t requested_ring_size);
int cb_grow(struct cb **cb, size_t min_ring_size);
int cb_shrink(struct cb **cb, size_t min_ring_size);
int cb_shrink_auto(struct cb **cb);

int cb_append(struct cb **cb, void *p, size_t len);

int cb_memalign(struct cb **cb,
                cb_offset_t *offset,
                size_t alignment,
                size_t size);

CB_INLINE size_t cb_ring_size(const struct cb *cb)
{
    return cb->mask + 1;
}


CB_INLINE size_t cb_loop_size(const struct cb *cb)
{
    return cb->loop_size;
}


CB_INLINE void* cb_ring_start(const struct cb *cb)
{
    return (char*)cb + cb->header_size;
}


CB_INLINE void* cb_ring_end(const struct cb *cb)
{
    return (char*)cb_ring_start(cb) + cb_ring_size(cb);
}


CB_INLINE void* cb_loop_start(const struct cb *cb)
{
    return cb_ring_end(cb);
}


CB_INLINE void* cb_loop_end(const struct cb *cb)
{
    return (char*)cb_ring_end(cb) + cb_loop_size(cb);
}


CB_INLINE size_t cb_data_size(const struct cb *cb)
{
    return cb->cursor - cb->data_start;
}


CB_INLINE cb_offset_t cb_cursor(const struct cb *cb)
{
    return cb->cursor;
}


CB_INLINE void cb_rewind_to(struct cb *cb, cb_offset_t offset)
{
    assert(cb_offset_lte(offset, cb->cursor));
    cb->cursor = offset;
}


CB_INLINE size_t cb_free_size(const struct cb *cb)
{
    return cb_ring_size(cb) - cb_data_size(cb);
}


CB_INLINE void* cb_at(const struct cb *cb, cb_offset_t offset)
{
    /* offset >= data_start */
    assert(cb_offset_cmp(offset, cb->data_start) > -1);

    /* offset <= data_end */
    assert(cb_offset_cmp(offset, cb->data_start + cb_ring_size(cb)) < 1);

    return (char*)cb_ring_start(cb) + (offset & cb->mask);
}


CB_INLINE cb_offset_t cb_from(const struct cb *cb, const void *addr)
{
    assert((char*)addr >= (char*)cb_ring_start(cb));
    assert((char*)addr <= (char*)cb_ring_end(cb));

    return cb->data_start + ((char*)addr - (char*)cb_ring_start(cb));
}


CB_INLINE int cb_ensure_free(struct cb **cb, size_t len)
{
    if (len <= cb_free_size(*cb))
        return 0;

    return cb_grow(cb, cb_data_size(*cb) + len);
}


CB_INLINE int cb_ensure_to(struct cb **cb, cb_offset_t offset)
{
    if (!cb_offset_lte((*cb)->cursor, offset))
        return -1;

    return cb_ensure_free(cb, offset - (*cb)->cursor);
}

#endif /* ! defined _CB_H_*/
