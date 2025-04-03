#include "cb_thread.h"

__thread struct cb        *thread_cb            = NULL;
__thread struct cb_at_immed_param_t thread_cb_at_immed_param;
__thread struct cb_region  thread_region;
__thread cb_offset_t       thread_cutoff_offset = (cb_offset_t)0ULL;
