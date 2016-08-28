#ifndef _CB_MAP_H_
#define _CB_MAP_H_

#include "cb.h"

/* Immediate version */

/*
 * There is no equivalent of "NULL" for cb_offset_t, so we use an invalid
 * value as the sentinel.  The reason that the value of 1 is invalid is because
 * the offsets of cb_bst_node structs must have an alignment greater than
 * char-alignment, but 1 is not aligned to anything greater than char
 * alignment.  Declared as an enum for use as a constant.
 */
enum
{
    CB_BST_SENTINEL = 1
};

int cb_bst_insert(struct cb             **cb,
                  cb_offset_t            *root_node_offset,
                  cb_offset_t             cutoff_offset,
                  const struct cb_key    *key,
                  const struct cb_value  *value);

int cb_bst_lookup(const struct cb     *cb,
                  cb_offset_t          root_node_offset,
                  const struct cb_key *key,
                  struct cb_value     *value);

int cb_bst_delete(struct cb             **cb,
                  cb_offset_t            *root_node_offset,
                  cb_offset_t             cutoff_offset,
                  const struct cb_key    *key);


/* Lazy version */
struct cb_map
{
    struct cb   **cb;
    cb_offset_t   last_command_offset;
};

int cb_map_init(struct cb_map *cb_map, struct cb **cb);

int cb_map_kv_set(struct cb_map         *cb_map,
                  const struct cb_key   *k,
                  const struct cb_value *v);

int cb_map_kv_lookup(const struct cb_map *cb_map,
                     const struct cb_key *k,
                     struct cb_value     *v);

int cb_map_kv_delete(struct cb_map       *cb_map,
                     const struct cb_key *k);

typedef int (*cb_map_traverse_func_t)(const struct cb_key   *k,
                                      const struct cb_value *v,
                                      void                  *closure);

int cb_map_traverse(struct cb_map          *cb_map,
                    cb_map_traverse_func_t  func,
                    void                   *closure);

int cb_map_consolidate(struct cb_map *cb_map);

void cb_map_print(const struct cb_map *cb_map);

#endif /* ! defined _CB_MAP_H_*/
