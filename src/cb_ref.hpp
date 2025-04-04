/**
 * @file cb_ref.hpp
 * @brief Defines different types of references/pointers into the continuous buffer (cb).
 *
 * This file provides several ways to reference data within the continuous buffer:
 * - CBO: Continuous Buffer offset-based pointer. Stores only the offset.
 * - CBP: Continuous Buffer pointer. Holds a direct pointer, offset, and CB instance. Non-rewritable.
 * - RCBP: Rewritable Continuous Buffer pointer. Extends CBP, adding linked list pointers
 *         to allow rewriting during buffer resize operations.
 *
 * Abbreviations:
 * - CBO: Continuous Buffer offset-based pointer
 * - RCBP: Rewritable Continuous Buffer pointer
 * - CBP: Continuous Buffer pointer
 */
#ifndef _CB_REF_HPP_
#define _CB_REF_HPP_

#include <assert.h>

#include "cb.h"
#include "cb_thread.h"

/**
 * @brief Represents a null offset within the continuous buffer.
 */
#define CB_NULL ((cb_offset_t)0)


#ifdef __cplusplus
extern "C" {
#endif

struct rcbp;

/**
 * @internal
 * @brief Adds an RCBP item to the global linked list of RCBPs.
 * @param item The RCBP item to add.
 * @note This function is intended for internal use by the RCBP constructor.
 */
void rcbp_add(struct rcbp *item);

/**
 * @internal
 * @brief Removes an RCBP item from the global linked list of RCBPs.
 * @param item The RCBP item to remove.
 * @note This function is intended for internal use by the RCBP destructor.
 */
void rcbp_remove(struct rcbp *item);

/**
 * @internal
 * @brief Rewrites the offsets and pointers of all tracked RCBPs after a continuous buffer resize.
 * @param new_cb Pointer to the new continuous buffer instance.
 * @note This function is intended for internal use only, typically called during
 *       continuous buffer resize operations.
 */
void rcbp_rewrite_list(struct cb *new_cb);

#ifdef __cplusplus
}  // extern "C"
#endif


#ifdef __cplusplus

/**
 * @internal
 * @brief Base structure for a Continuous Buffer Pointer (CBP).
 *
 * Holds a direct pointer to the data, the offset within the buffer, and a pointer
 * to the continuous buffer instance it belongs to. This structure itself is
 * intended for internal use; use the CBP or RCBP template for type safety.
 */
struct cbp
{
  void        *pointer_; ///< Direct pointer to the data in the buffer. NULL if offset is CB_NULL.
  cb_offset_t  offset_;  ///< Offset of the data within the continuous buffer.
  struct cb   *cb_;      ///< Pointer to the continuous buffer instance.

  /**
   * @brief Constructs a CBP using the thread-local continuous buffer.
   * @param offset The offset of the data within the thread-local buffer.
   */
  cbp(cb_offset_t offset)
    : pointer_(offset == CB_NULL ? NULL : cb_at_immed(&thread_cb_at_immed_param, offset)),
      offset_(offset),
      cb_(thread_cb)
  { }

  /**
   * @brief Constructs a cbp using a specific continuous buffer instance.
   * @param offset The offset of the data within the specified buffer.
   * @param cb Pointer to the continuous buffer instance.
   */
  cbp(cb_offset_t offset, struct cb *cb)
    : pointer_(offset == CB_NULL ? NULL : cb_at(cb, offset)),
      offset_(offset),
      cb_(cb)
  { }

  /**
   * @brief Copy constructor.
   * @param rhs The cbp instance to copy from.
   */
  cbp(cbp const &rhs)
    : pointer_(rhs.pointer_),
      offset_(rhs.offset_),
      cb_(rhs.cb_)
  {
    assert((offset_ == CB_NULL && pointer_ == NULL) || pointer_ == cb_at(cb_, offset_));
  }

  /**
   * @brief Checks if the pointer is null (i.e., the offset is CB_NULL).
   * @return True if the pointer is null, false otherwise.
   */
  bool is_nil() {
    return !pointer_;
  }
};

/**
 * @brief Typed, non-rewritable Continuous Buffer Pointer (CBP).
 *
 * Provides a type-safe wrapper around the internal `cbp` structure.
 * This pointer holds a direct reference to data within a continuous buffer
 * but is not automatically updated if the buffer resizes.
 *
 * @tparam T The type of the data being pointed to.
 */
template<typename T>
struct CBP : cbp
{
  /**
   * @brief Default constructor, initializes to null.
   */
  CBP() : cbp(CB_NULL) { }

  /**
   * @brief Constructs a CBP using the thread-local continuous buffer.
   * @param offset The offset of the data within the thread-local buffer.
   */
  CBP(cb_offset_t offset) : cbp(offset) { }

  /**
   * @brief Constructs a CBP using a specific continuous buffer instance.
   * @param offset The offset of the data within the specified buffer.
   * @param cb Pointer to the continuous buffer instance.
   */
  CBP(cb_offset_t offset, struct cb *cb) : cbp(offset, cb) { }

  /**
   * @brief Copy constructor.
   * @param rhs The CBP instance to copy from.
   */
  CBP(CBP<T> const &rhs) : cbp(rhs) { }

  /**
   * @brief Default assignment operator.
   */
  CBP<T>& operator=(const CBP<T> &rhs) = default;

  /**
   * @brief Gets a const pointer to the underlying data.
   * @return Const pointer of type T.
   */
  const T* cp() {
    return static_cast<const T*>(pointer_);
  }

  /**
   * @brief Gets a mutable pointer to the underlying data.
   * @return Mutable pointer of type T.
   */
  T* mp() {
    return static_cast<T*>(pointer_);
  }
};

/**
 * @internal
 * @brief Base structure for a Rewritable Continuous Buffer Pointer (RCBP).
 *
 * Extends `cbp` by adding pointers for a doubly-linked list (`prev_`, `next_`).
 * This list allows the system to find and update all active RCBPs when the
 * underlying continuous buffer is resized. This structure itself is intended
 * for internal use; use the RCBP template for type safety.
 */
struct rcbp : cbp
{
  rcbp *prev_; ///< Pointer to the previous RCBP in the global list.
  rcbp *next_; ///< Pointer to the next RCBP in the global list.

  /**
   * @brief Constructs an RCBP using the thread-local continuous buffer and adds it to the global list.
   * @param offset The offset of the data within the thread-local buffer.
   */
  rcbp(cb_offset_t offset) : cbp(offset) { rcbp_add(this); }

  /**
   * @brief Constructs an RCBP using a specific continuous buffer instance and adds it to the global list.
   * @param offset The offset of the data within the specified buffer.
   * @param cb Pointer to the continuous buffer instance.
   */
  rcbp(cb_offset_t offset, struct cb *cb) : cbp(offset, cb) { rcbp_add(this); }

  /**
   * @brief Copy constructor. Copies the pointer data and adds the new RCBP to the global list.
   * @param rhs The cbp instance to copy pointer data from.
   */
  rcbp(cbp const &rhs) : cbp(rhs) { rcbp_add(this); }

  /**
   * @brief Destructor. Removes the RCBP from the global list.
   */
  ~rcbp() { rcbp_remove(this); }
};

/**
 * @brief Typed, Rewritable Continuous Buffer Pointer (RCBP).
 *
 * Provides a type-safe wrapper around the internal `rcbp` structure.
 * This pointer holds a direct reference to data within a continuous buffer
 * and is automatically updated (rewritten) if the buffer resizes, thanks to
 * the underlying linked list mechanism managed by `rcbp_add`, `rcbp_remove`,
 * and `rcbp_rewrite_list`.
 *
 * @tparam T The type of the data being pointed to.
 */
template<typename T>
struct RCBP : rcbp
{
  /**
   * @brief Default constructor, initializes to null and adds to the list.
   */
  RCBP() : rcbp(CB_NULL) { }

  /**
   * @brief Constructs an RCBP using the thread-local continuous buffer and adds it to the list.
   * @param offset The offset of the data within the thread-local buffer.
   */
  RCBP(cb_offset_t offset) : rcbp(offset) { }

  /**
   * @brief Constructs an RCBP using a specific continuous buffer instance and adds it to the list.
   * @param offset The offset of the data within the specified buffer.
   * @param cb Pointer to the continuous buffer instance.
   */
  RCBP(cb_offset_t offset, struct cb *cb) : rcbp(offset, cb) { }

  /**
   * @brief Copy constructor. Copies the pointer data and adds the new RCBP to the list.
   * @param rhs The RCBP instance to copy from.
   */
  RCBP(RCBP<T> const &rhs) : rcbp(rhs) { }

  /**
   * @brief Constructor from a base cbp. Copies pointer data and adds the new RCBP to the list.
   * @param rhs The cbp instance to copy pointer data from.
   */
  RCBP(cbp const &rhs) : rcbp(rhs) { }

  /**
   * @brief Gets a const pointer to the underlying data.
   * @return Const pointer of type T.
   */
  const T* cp() {
    return static_cast<const T*>(pointer_);
  }

  /**
   * @brief Gets a mutable pointer to the underlying data.
   * @return Mutable pointer of type T.
   */
  T* mp() {
    return static_cast<T*>(pointer_);
  }

  /**
   * @brief Assignment operator. Copies pointer data from another RCBP.
   * @param rhs The RCBP instance to assign from.
   * @return Reference to this RCBP instance.
   */
  RCBP<T>& operator=(const RCBP<T> &rhs) {
    pointer_ = rhs.pointer_;
    offset_ = rhs.offset_;
    cb_ = rhs.cb_;
    // Note: Does not remove/re-add from the list, just updates pointer data.
    return *this;
  }
};

/**
 * @brief Typed, Continuous Buffer Offset-based pointer (CBO).
 *
 * Stores only the offset (`cb_offset_t`) of data within a continuous buffer.
 * Dereferencing requires providing the continuous buffer instance (either implicitly
 * via the thread-local buffer or explicitly). This is generally safer and more
 * flexible than CBP/RCBP, especially across different threads or when the buffer
 * might resize, as it doesn't hold a direct pointer that could become invalid.  In
 * particular, for structures which themselves live in the continuous buffer, this
 * structure must be used instead of CBP or RCBP.
 *
 * @tparam T The type of the data being referenced by the offset.
 */
template<typename T>
struct CBO
{
  cb_offset_t offset_; ///< The offset of the data within the continuous buffer.

  /**
   * @brief Default constructor, initializes to null offset.
   */
  CBO() : offset_(CB_NULL) { }

  /**
   * @brief Constructs a CBO from a given offset.
   * @param offset The offset value.
   */
  CBO(cb_offset_t offset) : offset_(offset) { }

  /**
   * @brief Copy constructor.
   * @param rhs The CBO instance to copy from.
   */
  CBO(CBO<T> const &rhs) : offset_(rhs.offset_) { }

  /**
   * @brief Assignment operator.
   * @param rhs The CBO instance to assign from.
   * @return Reference to this CBO instance.
   */
  //FIXME did this ever work? constexpr CBO<T>& operator=(const CBO<T> &rhs) {
  CBO<T>& operator=(const CBO<T> &rhs) {
    offset_ = rhs.offset_;
    return *this;
  }

  /**
   * @brief Checks if the offset is null (CB_NULL).
   * @return True if the offset is null, false otherwise.
   */
  bool is_nil() const {
    return (offset_ == CB_NULL);
  }

  /**
   * @brief Gets the underlying const offset value.
   * @return The offset value.
   */
  cb_offset_t co() const {
    return offset_;
  }

  /**
   * @brief Gets the underlying mutable offset value.
   * @return The offset value.
   */
  cb_offset_t mo() const {
    return offset_;
  }

  /**
   * @brief Dereferences the offset using the thread-local continuous buffer (local pointer).
   * @return A CBP<const T> pointing to the data in the thread-local buffer.
   */
  CBP<const T> clp() const {
    //return static_cast<const T*>(cb_at(thread_cb, offset_));
    return CBP<const T>(offset_);
  }

  /**
   * @brief Dereferences the offset using the thread-local continuous buffer (local pointer).
   * @return A CBP<T> pointing to the data in the thread-local buffer.
   */
  CBP<T> mlp() {
    //return static_cast<T*>(cb_at(thread_cb, offset_));
    return CBP<T>(offset_);
  }

  /**
   * @brief Dereferences the offset using a specified remote continuous buffer.
   * @param remote_cb Pointer to the remote continuous buffer instance.
   * @return A CBP<const T> pointing to the data in the specified remote buffer.
   */
  CBP<const T> crp(struct cb *remote_cb) const {
    return CBP<const T>(offset_, remote_cb);
  }

  /**
   * @brief Dereferences the offset using a specified remote continuous buffer.
   * @param remote_cb Pointer to the remote continuous buffer instance.
   * @return A CBP<T> pointing to the data in the specified remote buffer.
   */
  CBP<T> mrp(struct cb *remote_cb) const {
    return CBP<T>(offset_, remote_cb);
  }
};

#endif /* __cplusplus */

#endif /* _CB_REF_HPP_ */
