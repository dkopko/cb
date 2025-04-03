#ifndef _CB_THREAD_H_
#define _CB_THREAD_H_

#include "cb.h"
#include "cb_region.h"

extern __thread struct cb        *thread_cb;
extern __thread struct cb_at_immed_param_t thread_cb_at_immed_param;
extern __thread struct cb_region  thread_region;
extern __thread cb_offset_t       thread_cutoff_offset;

#endif /* _CB_THREAD_H_ */
