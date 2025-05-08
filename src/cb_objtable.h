#ifndef _CB_OBJTABLE_H_
#define _CB_OBJTABLE_H_

#include "cb.h"
#include "cb_ref.hpp"
#include "cb_structmap_amt.h"
#include "cb_thread.h"

typedef struct { uint64_t id; } ObjID;

#define CB_NULL_OID ((ObjID) { 0 })

typedef cb_structmap_amt<19, 5> ObjTableSM;

typedef struct ObjTableLayer {
  cb_offset_t  sm_offset;
  ObjTableSM  *sm;
} ObjTableLayer;

typedef cb_offset_t (*cb_derive_mutable_obj_layer_t)(struct cb **cb, struct cb_region *region, ObjID id, cb_offset_t object_offset);

typedef struct ObjTable {
  ObjTableLayer a;
  ObjTableLayer b;
  ObjTableLayer c;
  ObjID         next_obj_id;
  cb_structmap_amt_value_size_t value_size_func;
  cb_derive_mutable_obj_layer_t derive_mutable_obj_layer_func;
} ObjTable;

int objtablelayer_init(ObjTableLayer *layer,
                       struct cb *cb,
                       cb_offset_t sm_offset,
                       cb_structmap_amt_value_size_t value_size_func);
void objtablelayer_recache(ObjTableLayer *layer, struct cb *cb);
int objtablelayer_assign(ObjTableLayer *dest, ObjTableLayer *src);

typedef int (*objtablelayer_traverse_func_t)(uint64_t key, uint64_t value, void *closure);
int objtablelayer_traverse(const struct cb                **cb,
                           ObjTableLayer                   *layer,
                           objtablelayer_traverse_func_t   func,
                           void                            *closure);

size_t objtablelayer_external_size(ObjTableLayer *layer);
size_t objtablelayer_internal_size(ObjTableLayer *layer);
size_t objtablelayer_size(ObjTableLayer *layer);
void objtablelayer_external_size_adjust(ObjTableLayer *layer, ssize_t adjustment);
// NOTE: The following is used to pre-align a region's cursor before an
// objtablelayer_insert() in copy_objtable_b() and copy_objtable_c_not_in_b()
// for the sake of accurately tracking in Debug builds how much of the region is
// being consumed by that insertion.
extern inline size_t objtablelayer_insertion_alignment_get() { return 8; }


extern inline int
objtablelayer_insert(struct cb        **cb,
                     struct cb_region  *region,
                     ObjTableLayer     *layer,
                     uint64_t           key,
                     uint64_t           value)
{
  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));
  return layer->sm->insert(cb, region, key, value);
}

extern inline bool
objtablelayer_lookup(const struct cb *cb,
                     ObjTableLayer   *layer,
                     uint64_t         key,
                     uint64_t        *value)
{
  assert(layer->sm == (ObjTableSM*)cb_at(cb, layer->sm_offset));
  return (layer->sm->lookup(cb, key, value) && *value != CB_NULL);
}

void objtable_init(ObjTable *obj_table,
                   struct cb *cb,
                   cb_offset_t a_offset,
                   cb_offset_t b_offset,
                   cb_offset_t c_offset,
                   cb_structmap_amt_value_size_t value_size_func,
                   cb_derive_mutable_obj_layer_t derive_mutable_obj_layer_func);
void objtable_recache(ObjTable *obj_table, struct cb *cb);
void objtable_add_at(ObjTable *obj_table, ObjID obj_id, cb_offset_t offset);
ObjID objtable_add(ObjTable *obj_table, cb_offset_t offset);
cb_offset_t objtable_lookup(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_A(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_B(ObjTable *obj_table, ObjID obj_id);
cb_offset_t objtable_lookup_C(ObjTable *obj_table, ObjID obj_id);
void objtable_invalidate(ObjTable *obj_table, ObjID obj_id);
void objtable_external_size_adjust_A(ObjTable *obj_table, ssize_t adjustment);
void objtable_freeze(ObjTable *obj_table, struct cb **cb, struct cb_region *region);
size_t objtable_consolidation_size(ObjTable *obj_table);
cb_offset_t objtable_resolve_as_mutable(ObjID objid);


template<typename T>
struct OID
{
  ObjID objid_;

  OID() : objid_(CB_NULL_OID) { }

  OID(ObjID objid) : objid_(objid) { }

  OID(OID<T> const &rhs) : objid_(rhs.objid_) { }

  //FIXME did this ever work?? constexpr OID<T>& operator=(const OID<T> &rhs) {
  OID<T>& operator=(const OID<T> &rhs) {
    objid_ = rhs.objid_;
    return *this;
  }

  bool is_nil() const {
    return (objid_.id == CB_NULL_OID.id);
  }

  bool is_valid() const {
    return (!is_nil() && co() != CB_NULL);
  }

  //Underlying id
  ObjID id() const {
    return objid_;
  }

  //Underlying offset
  cb_offset_t co() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  //Underlying offset (alternative objtable)
  cb_offset_t co_alt(ObjTable *ot) const {
    return objtable_lookup(ot, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_A() const {
    return objtable_lookup_A(&thread_objtable, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_B() const {
    return objtable_lookup_B(&thread_objtable, objid_);
  }

  //Underlying offset, if this ObjID exists in A region mapping. CB_NULL otherwise.
  cb_offset_t co_C() const {
    return objtable_lookup_C(&thread_objtable, objid_);
  }

  //Underlying offset
  cb_offset_t mo() const {
    return objtable_lookup(&thread_objtable, objid_);
  }

  //Local dereference
  CBP<const T> clip() const {
    return CBP<const T>(co());
  }

  //Remote dereference
  CBP<const T> crip(struct cb *remote_cb) const {
    return CBP<const T>(co(), remote_cb);
  }

  //Remote dereference, alternative objtable
  CBP<const T> crip_alt(struct cb *remote_cb, ObjTable *ot) const {
    return CBP<const T>(co_alt(ot), remote_cb);
  }

  //Local dereference, region A only
  CBP<const T> clipA() const {
    return CBP<const T>(co_A());
  }

  //Local dereference, region B only
  CBP<const T> clipB() const {
    return CBP<const T>(co_B());
  }

  //Local dereference, region C only
  CBP<const T> clipC() const {
    return CBP<const T>(co_C());
  }

  //Local mutable dereference
  CBP<T> mlip() {
    //assert(exec_phase == EXEC_PHASE_INTERPRET);
    assert(on_main_thread);
    return CBP<T>(objtable_resolve_as_mutable(objid_));
  }

  //Remote dereference
  //T* rp(struct cb *remote_cb) {
  //  return static_cast<T*>(cb_at(remote_cb, offset_));
  //}
};

//FIXME these should move to the GC file.
#define ALREADY_WHITE_FLAG ((cb_offset_t)1)
#define ALREADY_WHITE(OFFSET) !!((OFFSET) & ALREADY_WHITE_FLAG)
#define PURE_OFFSET(OFFSET) ((OFFSET) & ~ALREADY_WHITE_FLAG)

#endif /* _CB_OBJTABLE_H_ */
