/* Copyright 2016 Daniel Kopko */
/*
 * This file is part of CB.
 *
 * CB is free software: you can redistribute it and/or modify it under the terms
 * of the GNU Lesser General Public License as published by the Free Software
 * Foundation, version 3 of the License.
 *
 * CB is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with CB.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _CB_MAP_H_
#define _CB_MAP_H_

#include "cb.h"
#include "cb_term.h"


/* Lazy version */

struct cb_map
{
    struct cb   **cb;
    cb_offset_t   last_command_offset;
};

int cb_map_init(struct cb_map *cb_map, struct cb **cb);

int cb_map_kv_set(struct cb_map        *cb_map,
                  const struct cb_term *key,
                  const struct cb_term *value);

int cb_map_kv_lookup(const struct cb_map  *cb_map,
                     const struct cb_term *key,
                     struct cb_term       *value);

int cb_map_kv_delete(struct cb_map        *cb_map,
                     const struct cb_term *key);

typedef int (*cb_map_traverse_func_t)(const struct cb_term *key,
                                      const struct cb_term *value,
                                      void                 *closure);

int cb_map_traverse(struct cb_map          *cb_map,
                    cb_map_traverse_func_t  func,
                    void                   *closure);

int cb_map_consolidate(struct cb_map *cb_map);

void cb_map_print(const struct cb_map *cb_map);

#endif /* ! defined _CB_MAP_H_*/
