#ifndef _CB_REF_HPP_
#define _CB_REF_HPP_

#include <assert.h>

#include "cb.h"
#include "cb_thread.h"


#define CB_NULL ((cb_offset_t)0)


#ifdef __cplusplus
extern "C" {
#endif

struct rcbp;

void rcbp_add(struct rcbp *item);
void rcbp_remove(struct rcbp *item);
void rcbp_rewrite_list(struct cb *new_cb);

#ifdef __cplusplus
}  // extern "C"
#endif


#ifdef __cplusplus

struct cbp
{
  void        *pointer_;
  cb_offset_t  offset_;
  struct cb   *cb_;

  cbp(cb_offset_t offset)
    : pointer_(offset == CB_NULL ? NULL : cb_at_immed(&thread_cb_at_immed_param, offset)),
      offset_(offset),
      cb_(thread_cb)
  { }

  cbp(cb_offset_t offset, struct cb *cb)
    : pointer_(offset == CB_NULL ? NULL : cb_at(cb, offset)),
      offset_(offset),
      cb_(cb)
  { }

  cbp(cbp const &rhs)
    : pointer_(rhs.pointer_),
      offset_(rhs.offset_),
      cb_(rhs.cb_)
  {
    assert((offset_ == CB_NULL && pointer_ == NULL) || pointer_ == cb_at(cb_, offset_));
  }

  bool is_nil() {
    return !pointer_;
  }
};


template<typename T>
struct CBP : cbp
{
  CBP() : cbp(CB_NULL) { }

  CBP(cb_offset_t offset) : cbp(offset) { }

  CBP(cb_offset_t offset, struct cb *cb) : cbp(offset, cb) { }

  CBP(CBP<T> const &rhs) : cbp(rhs) { }

  CBP<T>& operator=(const CBP<T> &rhs) = default;

  const T* cp() {
    return static_cast<const T*>(pointer_);
  }

  T* mp() {
    return static_cast<T*>(pointer_);
  }
};


struct rcbp : cbp
{
  rcbp *prev_;
  rcbp *next_;

  rcbp(cb_offset_t offset) : cbp(offset) { rcbp_add(this); }

  rcbp(cb_offset_t offset, struct cb *cb) : cbp(offset, cb) { rcbp_add(this); }

  rcbp(cbp const &rhs) : cbp(rhs) { rcbp_add(this); }

  ~rcbp() { rcbp_remove(this); }
};


template<typename T>
struct RCBP : rcbp
{
  RCBP() : rcbp(CB_NULL) { }

  RCBP(cb_offset_t offset) : rcbp(offset) { }

  RCBP(cb_offset_t offset, struct cb *cb) : rcbp(offset, cb) { }

  RCBP(RCBP<T> const &rhs) : rcbp(rhs) { }

  RCBP(cbp const &rhs) : rcbp(rhs) { }

  const T* cp() {
    return static_cast<const T*>(pointer_);
  }

  T* mp() {
    return static_cast<T*>(pointer_);
  }

  RCBP<T>& operator=(const RCBP<T> &rhs) {
    pointer_ = rhs.pointer_;
    offset_ = rhs.offset_;
    cb_ = rhs.cb_;
    return *this;
  }
};


template<typename T>
struct CBO
{
  cb_offset_t offset_;

  CBO() : offset_(CB_NULL) { }

  CBO(cb_offset_t offset) : offset_(offset) { }

  CBO(CBO<T> const &rhs) : offset_(rhs.offset_) { }

  //FIXME did this ever work? constexpr CBO<T>& operator=(const CBO<T> &rhs) {
  CBO<T>& operator=(const CBO<T> &rhs) {
    offset_ = rhs.offset_;
    return *this;
  }

  bool is_nil() const {
    return (offset_ == CB_NULL);
  }

  //Underlying offset
  cb_offset_t co() const {
    return offset_;
  }

  //Underlying offset
  cb_offset_t mo() const {
    return offset_;
  }

  //Local dereference
  CBP<const T> clp() const {
    //return static_cast<const T*>(cb_at(thread_cb, offset_));
    return CBP<const T>(offset_);
  }

  //Local dereference
  CBP<T> mlp() {
    //return static_cast<T*>(cb_at(thread_cb, offset_));
    return CBP<T>(offset_);
  }

  //Remote dereference
  CBP<const T> crp(struct cb *remote_cb) const {
    return CBP<const T>(offset_, remote_cb);
  }

  //Remote dereference
  CBP<T> mrp(struct cb *remote_cb) const {
    return CBP<T>(offset_, remote_cb);
  }
};

#endif /* __cplusplus */

#endif /* _CB_REF_HPP_ */
