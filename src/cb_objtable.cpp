#include "cb_objtable.h"

int
objtablelayer_init(ObjTableLayer *layer,
                   struct cb *cb,
                   cb_offset_t sm_offset,
                   cb_structmap_amt_value_size_t value_size_func)
{
  layer->sm_offset = sm_offset;
  layer->sm = (ObjTableSM*)cb_at(cb, layer->sm_offset);
  //layer->sm->init(&klox_allocation_size);
  layer->sm->init(value_size_func);
  return 0;
}

void
objtablelayer_recache(ObjTableLayer *layer, struct cb *cb)
{
  layer->sm = (ObjTableSM*)cb_at(cb, layer->sm_offset);
}

int
objtablelayer_assign(ObjTableLayer *dest, ObjTableLayer *src) {
  *dest = *src;
  assert(dest->sm == (ObjTableSM*)cb_at(thread_cb, dest->sm_offset));
  return 0;
}

int
objtablelayer_traverse(const struct cb                **cb,
                       ObjTableLayer                   *layer,
                       objtablelayer_traverse_func_t   func,
                       void                            *closure) {
  int ret;

  (void)ret;

  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));

  // Traverse the structmap entries.
  ret = layer->sm->traverse(cb,
                            (cb_structmap_amt_traverse_func_t)func,
                            closure);
  assert(ret == 0);

  return 0;
}

size_t
objtablelayer_external_size(ObjTableLayer *layer) {
  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));
  return layer->sm->external_size();
}

size_t
objtablelayer_internal_size(ObjTableLayer *layer) {
  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));
  return layer->sm->internal_size();
}

size_t
objtablelayer_size(ObjTableLayer *layer) {
  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));
  return layer->sm->size();
}

void
objtablelayer_external_size_adjust(ObjTableLayer *layer,
                                   ssize_t        adjustment)
{
  assert(layer->sm == (ObjTableSM*)cb_at(thread_cb, layer->sm_offset));
  layer->sm->external_size_adjust(adjustment);
}

void
objtable_init(ObjTable *obj_table,
              struct cb *cb,
              cb_offset_t a_offset,
              cb_offset_t b_offset,
              cb_offset_t c_offset,
              cb_structmap_amt_value_size_t value_size_func,
              cb_derive_mutable_obj_layer_t derive_mutable_obj_layer_func)
{
  objtablelayer_init(&(obj_table->a), cb, a_offset, value_size_func);
  objtablelayer_init(&(obj_table->b), cb, b_offset, value_size_func);
  objtablelayer_init(&(obj_table->c), cb, c_offset, value_size_func);
  obj_table->next_obj_id.id  = 1;
  obj_table->value_size_func = value_size_func;
  obj_table->derive_mutable_obj_layer_func = derive_mutable_obj_layer_func;
}

void
objtable_recache(ObjTable *obj_table, struct cb *cb)
{
  objtablelayer_recache(&(obj_table->a), cb);
  objtablelayer_recache(&(obj_table->b), cb);
  objtablelayer_recache(&(obj_table->c), cb);
}

void
objtable_add_at(ObjTable *obj_table, ObjID obj_id, cb_offset_t offset)
{
  //NOTE: This function breaks the abstraction of ObjTableLayer, as it peers
  // down past it to deal with the structmaps themselves.  Maybe it's worth
  // removing the ObjTableLayer abstraction.

  int ret;
  (void)ret;
  assert(obj_table->a.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->a.sm_offset));
  assert(obj_table->b.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->b.sm_offset));
  assert(obj_table->c.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->c.sm_offset));

  unsigned int pre_node_count = obj_table->a.sm->node_count();

  ret = objtablelayer_insert(&thread_cb, &thread_region, &(obj_table->a), obj_id.id, offset);
   assert(ret == 0);

  unsigned int post_node_count = obj_table->a.sm->node_count();
  assert(post_node_count >= pre_node_count);

  //Account for future structmap enlargement on merge due to slot collisions.
  unsigned int delta_node_count = post_node_count - pre_node_count;
  assert(post_node_count >= pre_node_count);
  unsigned int b_collide_node_count = obj_table->b.sm->would_collide_node_count(thread_cb, obj_id.id);
  unsigned int c_collide_node_count = obj_table->c.sm->would_collide_node_count(thread_cb, obj_id.id);
  unsigned int max_collide_node_count = (b_collide_node_count > c_collide_node_count ? b_collide_node_count : c_collide_node_count);
  if (max_collide_node_count > delta_node_count) {
    unsigned int addl_node_count = max_collide_node_count - delta_node_count;
    cb_log_debug("Need addl_nodes (objtable): %ju", (uintmax_t)addl_node_count);
    addl_collision_nodes += addl_node_count;
  }
}

ObjID
objtable_add(ObjTable *obj_table, cb_offset_t offset)
{
  ObjID obj_id = obj_table->next_obj_id;

  objtable_add_at(obj_table, obj_id, offset);
  (obj_table->next_obj_id.id)++;

  return obj_id;
}

cb_offset_t
objtable_lookup(ObjTable *obj_table, ObjID obj_id)
{
  uint64_t v;

  if (objtablelayer_lookup(thread_cb, &(obj_table->a), obj_id.id, &v) ||
      objtablelayer_lookup(thread_cb, &(obj_table->b), obj_id.id, &v) ||
      objtablelayer_lookup(thread_cb, &(obj_table->c), obj_id.id, &v)) {
    return PURE_OFFSET((cb_offset_t)v);
  }

  return CB_NULL;
}

cb_offset_t
objtable_lookup_A(ObjTable *obj_table, ObjID obj_id)
{
  uint64_t v;

  if (objtablelayer_lookup(thread_cb, &(obj_table->a), obj_id.id, &v))
    return PURE_OFFSET((cb_offset_t)v);

  return CB_NULL;
}

cb_offset_t
objtable_lookup_B(ObjTable *obj_table, ObjID obj_id)
{
  uint64_t v;

  if (objtablelayer_lookup(thread_cb, &(obj_table->b), obj_id.id, &v))
    return PURE_OFFSET((cb_offset_t)v);

  return CB_NULL;
}

cb_offset_t
objtable_lookup_C(ObjTable *obj_table, ObjID obj_id)
{
  uint64_t v;

  if (objtablelayer_lookup(thread_cb, &(obj_table->c), obj_id.id, &v))
    return PURE_OFFSET((cb_offset_t)v);

  return CB_NULL;
}

void
objtable_invalidate(ObjTable *obj_table, ObjID obj_id)
{
  objtable_add_at(obj_table, obj_id, CB_NULL);
}

void
objtable_external_size_adjust_A(ObjTable *obj_table, ssize_t adjustment)
{
    objtablelayer_external_size_adjust(&(obj_table->a), adjustment);
}

void
objtable_freeze(ObjTable *obj_table, struct cb **cb, struct cb_region *region)
{
  int ret;

  (void)ret;

  cb_offset_t new_a_offset;

  ret = cb_region_memalign(cb, region, &new_a_offset, alignof(ObjTableSM), sizeof(ObjTableSM));
  assert(ret == CB_SUCCESS);

  //assert(num_entries(obj_table->c) == 0); //FIXME create this check
  objtablelayer_assign(&(obj_table->c), &(obj_table->b));
  objtablelayer_assign(&(obj_table->b), &(obj_table->a));
  objtablelayer_init(&(obj_table->a), *cb, new_a_offset, obj_table->value_size_func);

  //Track only new additional collision nodes.
  snap_addl_collision_nodes = addl_collision_nodes;
  addl_collision_nodes = 0;
}

size_t
objtable_consolidation_size(ObjTable *obj_table)
{
  assert(obj_table->a.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->a.sm_offset));
  assert(obj_table->b.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->b.sm_offset));
  assert(obj_table->c.sm == (ObjTableSM*)cb_at(thread_cb, obj_table->c.sm_offset));

  size_t b_external_size = obj_table->b.sm->external_size();
  size_t b_internal_size = obj_table->b.sm->internal_size();
  size_t c_external_size = obj_table->c.sm->external_size();
  size_t c_internal_size = obj_table->c.sm->internal_size();
  size_t addl_size       = snap_addl_collision_nodes * (sizeof(ObjTableSM::node) + alignof(ObjTableSM::node) - 1);

  cb_log_debug("objtable b_external_size: %zu, b_internal_size: %zu, c_external_size: %zu, c_internal_size: %zu, modification_size: %zu, addl_size: %zu",
         b_external_size, b_internal_size, c_external_size, c_internal_size, ObjTableSM::MODIFICATION_MAX_SIZE, addl_size);

  //NOTE: All objtablelayer's structmaps must have the same number of firstlevel bits.
  //NOTE: One MODIFICATION_MAX_SIZE encompasses the space need for the mutations themselves. The other is because the GC itself will *also* reserve MODIFICATION_MAX_SIZE on insertion.
  return b_external_size + b_internal_size + c_external_size + c_internal_size + (2 * ObjTableSM::MODIFICATION_MAX_SIZE) + addl_size;
}

cb_offset_t
objtable_resolve_as_mutable(ObjID objid)
{
  cb_offset_t o;

  assert(on_main_thread);
  //assert(exec_phase == EXEC_PHASE_COMPILE || exec_phase == EXEC_PHASE_INTERPRET || exec_phase == EXEC_PHASE_FREE_WHITE_SET);

  o = objtable_lookup_A(&thread_objtable, objid);
  if (o != CB_NULL) {
    //KLOX_TRACE("#%ju@%ju found in objtable A\n", (uintmax_t)objid.id, (uintmax_t)o);
    assert(cb_offset_cmp(o, thread_cutoff_offset) > 0);
    return o;
  }

  o = objtable_lookup_B(&thread_objtable, objid);
  if (o != CB_NULL) {
    //KLOX_TRACE("#%ju@%ju found in objtable B\n", (uintmax_t)objid.id, (uintmax_t)o);
    cb_offset_t layer_o = thread_objtable.derive_mutable_obj_layer_func(&thread_cb, &thread_region, objid, o);
    assert(cb_offset_cmp(layer_o, thread_cutoff_offset) > 0);
    objtable_add_at(&thread_objtable, objid, layer_o);
    //KLOX_TRACE("#%ju@%ju is new mutable layer in objtable A\n", (uintmax_t)objid_.id, layer_o);
    //KLOX_TRACE_ONLY(printObjectValue(OBJ_VAL(objid)));
    //KLOX_TRACE_(" is new mutable layer in objtable A\n");
    return layer_o;
  }

  o = objtable_lookup_C(&thread_objtable, objid);
  assert(o != CB_NULL);
  //KLOX_TRACE("#%ju@%ju found in objtable C\n", (uintmax_t)objid.id, (uintmax_t)o);
  cb_offset_t layer_o = thread_objtable.derive_mutable_obj_layer_func(&thread_cb, &thread_region, objid, o);
  assert(cb_offset_cmp(layer_o, thread_cutoff_offset) > 0);
  objtable_add_at(&thread_objtable, objid, layer_o);
  //KLOX_TRACE("#%ju@%ju is new mutable layer in objtable A\n", (uintmax_t)objid_.id, layer_o);
  //KLOX_TRACE_ONLY(printObjectValue(OBJ_VAL(objid)));
  //KLOX_TRACE_(" is new mutable layer in objtable A\n");
  return layer_o;
}