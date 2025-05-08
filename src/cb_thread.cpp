#include "cb_thread.h"
#include "cb_objtable.h"

__thread struct cb        *thread_cb            = NULL;
__thread struct cb_at_immed_param_t thread_cb_at_immed_param;
__thread struct cb_region  thread_region;
__thread cb_offset_t       thread_cutoff_offset = (cb_offset_t)0ULL;
__thread struct ObjTable   thread_objtable;
__thread unsigned int      addl_collision_nodes;
__thread unsigned int      snap_addl_collision_nodes;
