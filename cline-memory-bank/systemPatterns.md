# System Patterns: cb

## Memory Architecture

The core innovation in cb is its continuous buffer approach combined with a handle-based reference system, enabling wait-free garbage collection.

### Continuous Buffer Design

1. **Power-of-2 Sized Ring Buffer**
   - Implemented as `struct cb`
   - Uses efficient mask-based wrapping (instead of expensive modulo)
   - Cursor-based allocation with O(1) cost
   - "Magic ring buffer" implementation to avoid bounds checking

2. **Serialized Offsets**
   - Uses `cb_offset_t` to represent locations within the ring
   - Serial number arithmetic for reliable offset comparisons
   - Offsets retained in "pre-masked" state to maintain order relationships

3. **Region-Based Memory Management**
   - `struct cb_region` facilitates sub-allocations within the buffer
   - Multiple regions can exist within a single buffer
   - Regions move through the buffer in phases

### Cutoff Offset Mechanism

The `cutoff_offset` is a critical concept that:
- Defines the boundary between mutable and immutable memory
- Offsets lower than the cutoff must be treated as immutable
- Offsets higher than the cutoff can be mutated in place
- Helps reduce unnecessary copying of data
- Objects comprised of both mutable nodes (above the cutoff_offset) and immutable nodes (below the cutoff_offset) are termed "partially persistent"

## Key Data Structures

### Partially-Persistent Red-Black Tree (`cb_bst`)

- Self-balancing binary search tree with red-black properties
- Supports efficient path-copying for modifications
- Core data structure for implementing collections
- O(log n) complexity for lookup, insert, and delete operations

## Design Decisions and Tradeoffs

### Performance Shift

The design explicitly shifts costs:
- GC becomes an O(1) operation from the main thread's perspective
- Allocation remains an O(1) operation
- Object dereferencing becomes O(log32(n)) rather than O(1)

### Memory Management Constraints

- Must use power-of-2 sized ring buffer
- Buffer resizing is possible but introduces pauses
- Careful estimation of memory needs recommended
- Immutable data older than cutoff_offset

### Implementation Complexity

- Complex reference handling with indirection
- Careful synchronization between threads
- Serial number arithmetic for correct offset management
- Path-copying overhead for persistent data structures

## Error Handling and Safety

- Heavy use of assertions to catch invariant violations
- Careful management of cutoff_offset to prevent invalid mutations
- Protection against buffer wraparound issues through magic ring buffer
- Thread synchronization mechanisms to prevent race conditions
