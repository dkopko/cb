#include "cb_ref.hpp"

#include "cb_log.h"

static __thread struct rcbp *thread_rcbp_list = NULL;

void
rcbp_add(struct rcbp *item) {
  if (thread_rcbp_list)
    thread_rcbp_list->prev_ = item;
  item->next_ = thread_rcbp_list;
  item->prev_ = NULL;
  thread_rcbp_list = item;
}

void
rcbp_remove(struct rcbp *item) {
  if (item->prev_)
    item->prev_->next_ = item->next_;
  if (item->next_)
    item->next_->prev_ = item->prev_;
  if (thread_rcbp_list == item)
    thread_rcbp_list = item->next_;
}

void
rcbp_rewrite_list(struct cb *new_cb)
{
  struct rcbp *item = thread_rcbp_list;

  cb_log_debug("BEGIN REWRITE LIST");
  while (item) {
    if (item->offset_ != CB_NULL) {
      void *new_pointer = cb_at(new_cb, item->offset_);

      cb_log_debug("Rewriting pointer %p of cb:%p to %p of new_cb:%p",
                   item->pointer_, item->cb_, new_pointer, new_cb);

      item->pointer_ = new_pointer;
      item->cb_ = new_cb;
    } else {
      cb_log_debug("rewrite list item %p has CB_NULL offset, so not rewriting.", item);
    }

    item = item->next_;
  }
  cb_log_debug("END REWRITE LIST");
}


