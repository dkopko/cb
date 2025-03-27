/* Copyright 2025 Daniel Kopko */
/* Based on cb_bst.h and cb_structmap_amt.h */
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
#ifndef _CB_HAMT_H_
#define _CB_HAMT_H_

#include "cb.h"
#include "cb_hash.h"
#include "cb_region.h"
#include "cb_term.h"
#include "cb_bits.h" // For cb_popcount64

// Define the missing hasher type based on cb_term_hash signature
typedef cb_hash_t (*cb_term_hasher_t)(const struct cb *cb, const struct cb_term *term);

#ifdef __cplusplus
extern "C" {
#endif

// --- Constants ---

// Use 5 bits per level for a 32-way branching factor.
#define CB_HAMT_BITS_PER_LEVEL 5
#define CB_HAMT_ENTRIES_PER_NODE (1 << CB_HAMT_BITS_PER_LEVEL) // 32
#define CB_HAMT_LEVEL_MASK (CB_HAMT_ENTRIES_PER_NODE - 1)
#define CB_HAMT_MAX_DEPTH ((64 + CB_HAMT_BITS_PER_LEVEL - 1) / CB_HAMT_BITS_PER_LEVEL) // Ceiling division

// Sentinel value for empty nodes/offsets, similar to cb_bst.
// Using 0 might be okay if alignment guarantees non-zero offsets for valid nodes/items.
// Let's stick to the cb_bst pattern for now.
enum
{
    CB_HAMT_SENTINEL = 1
};

// --- Entry Types and Structure ---

// Similar to cb_structmap_amt_entry_type
enum cb_hamt_entry_type
{
    CB_HAMT_ENTRY_EMPTY = 0, // Represents an unused slot
    CB_HAMT_ENTRY_ITEM  = 1, // Represents a key-value pair directly
    CB_HAMT_ENTRY_NODE  = 2  // Represents a pointer to a child node
};

// Store type in the lower 2 bits of the offset field. Assumes offsets are aligned >= 4.
#define CB_HAMT_TYPE_MASK 0x3

struct cb_hamt_item
{
    struct cb_term key;
    struct cb_term value;
    cb_hash_t      key_hash; // Store full key hash to detect potential collisions (though we won't resolve)
};

// A node contains a bitmap indicating populated entries and an array of offsets.
// The offsets point either to cb_hamt_item or cb_hamt_node structures.
struct cb_hamt_node
{
    uint32_t    bitmap; // Bitmap indicating which slots are populated
    cb_offset_t entries[CB_HAMT_ENTRIES_PER_NODE]; // Offsets to items or child nodes
                                                   // Storing offsets directly might be less compact than
                                                   // a packed array based on popcount(bitmap), but simpler.
                                                   // Let's use the simpler approach first.
};

// --- Header Structure ---

// Similar to cb_bst_header, but without comparators. Includes hash/render/size functions.
struct cb_hamt_header
{
    size_t                  total_internal_size;    // Size of header + all nodes + all items
    size_t                  total_external_size;    // Size of external data referenced by terms
    unsigned int            num_entries;            // Count of key-value pairs
    cb_hash_t               hash_value;             // Hash of all key-value pairs (order independent)
    cb_term_hasher_t        key_term_hash;          // Function to hash keys
    cb_term_render_t        key_term_render;        // Function to render keys
    cb_term_render_t        value_term_render;      // Function to render values
    cb_term_external_size_t key_term_external_size; // Function for external size of keys
    cb_term_external_size_t value_term_external_size;// Function for external size of values
    cb_offset_t             root_node_offset;       // Offset to the root node (or CB_HAMT_SENTINEL if empty)
    uint32_t                root_bitmap;            // Bitmap for the root level (can optimize root access)
                                                    // We might embed the first level directly in the header
                                                    // like cb_structmap_amt, but let's start with a root node offset.
};

// --- Inline Helper Functions ---

CB_INLINE struct cb_hamt_header*
cb_hamt_header_at(const struct cb *cb, cb_offset_t header_offset)
{
    if (header_offset == CB_HAMT_SENTINEL) return NULL;
    return (struct cb_hamt_header*)cb_at(cb, header_offset);
}

CB_INLINE struct cb_hamt_node*
cb_hamt_node_at(const struct cb *cb, cb_offset_t node_offset)
{
    if (node_offset == CB_HAMT_SENTINEL) return NULL;
    // Assumes node_offset doesn't have type tags
    return (struct cb_hamt_node*)cb_at(cb, node_offset);
}

CB_INLINE struct cb_hamt_item*
cb_hamt_item_at(const struct cb *cb, cb_offset_t item_offset)
{
    if (item_offset == CB_HAMT_SENTINEL) return NULL;
    // Assumes item_offset doesn't have type tags
    return (struct cb_hamt_item*)cb_at(cb, item_offset);
}

CB_INLINE enum cb_hamt_entry_type
cb_hamt_entry_type_get(cb_offset_t entry_offset)
{
    // Check sentinel first
    if (entry_offset == CB_HAMT_SENTINEL) return CB_HAMT_ENTRY_EMPTY;
    // Extract type from lower bits
    return (enum cb_hamt_entry_type)(entry_offset & CB_HAMT_TYPE_MASK);
}

CB_INLINE cb_offset_t
cb_hamt_entry_offset_get(cb_offset_t entry_offset)
{
    // Mask out type bits
    return entry_offset & ~CB_HAMT_TYPE_MASK;
}

CB_INLINE cb_offset_t
cb_hamt_entry_offset_pack(cb_offset_t offset, enum cb_hamt_entry_type type)
{
    cb_assert((offset & CB_HAMT_TYPE_MASK) == 0); // Ensure offset is properly aligned
    return offset | type;
}

CB_INLINE cb_term_hasher_t
cb_hamt_key_hash_get(const struct cb *cb, cb_offset_t header_offset)
{
    struct cb_hamt_header *header = cb_hamt_header_at(cb, header_offset);
    if (!header || !header->key_term_hash) return &cb_term_hash; // Default hasher
    return header->key_term_hash;
}

// Add getters for render and external_size functions similar to cb_bst.h if needed...

// --- Public API ---

int
cb_hamt_init(struct cb               **cb,
             struct cb_region         *region,
             cb_offset_t              *new_header_offset_out,
             cb_term_hasher_t          key_term_hash,          // Required for HAMT
             cb_term_render_t          key_term_render,
             cb_term_render_t          value_term_render,
             cb_term_external_size_t   key_term_external_size,
             cb_term_external_size_t   value_term_external_size);

int
cb_hamt_insert(struct cb            **cb,
               struct cb_region      *region,
               cb_offset_t           *header_offset,
               cb_offset_t            cutoff_offset,
               const struct cb_term  *key,
               const struct cb_term  *value);

int
cb_hamt_lookup(const struct cb      *cb,
               cb_offset_t           header_offset,
               const struct cb_term *key,
               struct cb_term       *value_out); // Changed name for clarity

int
cb_hamt_delete(struct cb            **cb,
               struct cb_region      *region,
               cb_offset_t           *header_offset,
               cb_offset_t            cutoff_offset,
               const struct cb_term  *key);

bool
cb_hamt_contains_key(const struct cb      *cb,
                     cb_offset_t           header_offset,
                     const struct cb_term *key);

typedef int (*cb_hamt_traverse_func_t)(const struct cb_term *key,
                                       const struct cb_term *value,
                                       void                 *closure);

int
cb_hamt_traverse(const struct cb         *cb,
                 cb_offset_t              header_offset,
                 cb_hamt_traverse_func_t  func,
                 void                    *closure);

void
cb_hamt_print(struct cb   **cb,
              cb_offset_t   header_offset);

// Comparison function - compares content, not structure (like cb_bst_cmp)
int
cb_hamt_cmp(const struct cb      *cb,
            cb_offset_t           lhs_header_offset,
            cb_offset_t           rhs_header_offset);

// Size functions
size_t
cb_hamt_internal_size(const struct cb *cb, cb_offset_t header_offset);

size_t
cb_hamt_external_size(const struct cb *cb, cb_offset_t header_offset);

int
cb_hamt_external_size_adjust(struct cb   *cb,
                             cb_offset_t  header_offset,
                             ssize_t      adjustment);

size_t
cb_hamt_size(const struct cb *cb, cb_offset_t header_offset);

unsigned int
cb_hamt_num_entries(const struct cb *cb, cb_offset_t header_offset);

// Hashing functions (for the whole map content)
void
cb_hamt_hash_continue(cb_hash_state_t *hash_state,
                      const struct cb *cb,
                      cb_offset_t      header_offset);

cb_hash_t
cb_hamt_hash(const struct cb *cb, cb_offset_t header_offset);

// Rendering functions
int
cb_hamt_render(cb_offset_t   *dest_offset,
               struct cb    **cb,
               cb_offset_t    header_offset,
               unsigned int   flags);

const char*
cb_hamt_to_str(struct cb   **cb,
               cb_offset_t   header_offset);

// Iterators might be complex due to the structure. Deferring for now unless needed.

#ifdef __cplusplus
}  // extern "C"
#endif

#endif /* ! defined _CB_HAMT_H_ */
