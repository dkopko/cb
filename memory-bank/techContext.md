# Technical Context: cb

## Technology Stack

The cb project is implemented primarily in C, with some testing components using C++. The technical stack includes:

- **C**: Core implementation language
- **C++**: Used for some testing components
- **Make/CMake**: Build system
- **Python**: Used for testing and benchmarking utilities

## Core Components

### Continuous Buffer Implementation

- **cb.h/cb.c**: Defines the core ring buffer implementation
- **cb_region.h/cb_region.c**: Implements region-based allocation within the buffer
- **cb_bits.h**: Bit manipulation utilities for efficient operations
- **cb_misc.h**: Common utilities and macros

### Data Structures

- **cb_bst.h/cb_bst.c**: Partially-persistent red-black tree implementation
- **cb_structmap.h/cb_structmap.c**: Object ID to offset mapping (abandoned)
- **cb_lb_set.h/cb_lb_set.c**: Lower-bound set implementation (abandoned)
- **cb_map.h/cb_map.c**: Map implementation with key-value functionality (abandoned)

### Support Systems

- **cb_log.h/cb_log.c**: Logging and diagnostic facilities
- **cb_assert.h**: Assertion framework for invariant checking
- **cb_random.h/cb_random.c**: Random number generation for testing
- **cb_print.h/cb_print.c**: Pretty printing utilities
- **cb_hash.h**: Hashing utilities
- **cb_term.h/cb_term.c**: Facilities for working with terms (values) of a runtime.

### External Dependencies

The project incorporates several external dependencies in the `external/` directory:
- **cycle.h**: Cycle detection utilities
- **xxhash.h/xxhash.c**: Fast hash function implementation
- **freebsd-queue.h/tree.h**: BSD queue and tree implementations
- **openbsd-queue.h/tree.h**: OpenBSD queue and tree implementations

## Build and Development Environment

### Building cb

The project uses a standard Make-based build system:

```sh
# Clone the repository
git clone https://github.com/dkopko/cb
cd cb

# Build the library and tests
make -j

# Run tests
make test
```

### Directory Structure

- **src/**: Core library implementation
- **test/**: Test suite
- **scripts/**: Build and test scripts
- **external/**: Third-party dependencies
- **attic/**: Legacy or experimental code

## Technical Constraints

### Memory Management

- **Power-of-2 Sizing**: The continuous buffer must be sized to a power of 2
- **Magic Ring Buffer**: Implementation requires specific memory mapping techniques
- **Serial Number Arithmetic**: Critical for correct offset comparisons
- **Alignment Requirements**: Object allocations must account for alignment

### Concurrency Model

- **Thread Safety**: Careful coordination between main and GC threads
- **Lock-Free Communication**: Requires specialized synchronization mechanisms
- **Region Management**: Regions A, B, and C must be managed with specific rules
- **Cutoff Offset**: Governs which areas of memory are immutable vs. mutable

### Performance Considerations

- **Dereferencing Cost**: O(log32(n)) cost for object lookups
- **Buffer Resizing**: Expensive and causes pauses
- **Path Copying**: Required for modifications to persistent data structures
- **Memory Estimation**: Ideally, program memory size should be estimated in advance

## Optimization Techniques

- **Bump-Pointer Allocation**: Fast sequential allocation within regions
- **Memory Mapping Optimizations**: Avoid bounds checking through virtual memory tricks
- **Cutoff Offset**: Reduce unnecessary copying through mutability boundary
- **Persistent Data Structures**: Share unchanged portions to reduce memory usage

## Assertion-Oriented Programming

The codebase follows an "Assertion-Oriented Programming" style:
- Heavy use of assertions to catch invariant violations early
- Critical for debugging complex memory management
- Helps maintain correctness of the implementation

## Technical Documentation

### Code Naming Conventions

- **cb_**: Prefix for all library functions and types
- **cb_offset_t**: Type representing an offset within the ring buffer
- **cb_struct_id_t**: Type representing an object identifier (abandoned)
- Standard C naming conventions with snake_case for functions and variables

### Memory-Related Terminology

- **cutoff_offset**: Boundary between mutable and immutable memory
- **LTE/GTE**: Less/Greater Than or Equal in serial number arithmetic
- **magic ring buffer**: Memory-mapped technique to avoid bounds checking
- **partially persistent**: Data structures combining mutable and immutable nodes

## Testing Infrastructure

- **test/**: Contains unit and functional tests
- **test_unit_*.c**: Unit tests for specific components
- **test_measure_*.c**: Performance measurement tests
- **scripts/test_suite.sh**: Test runner script

## Known Technical Limitations

- Ring buffer resizing causes pauses
- Complex synchronization model requires careful programming
- Dereferencing cost higher than direct pointers
- Implementation complexity requires thorough testing
