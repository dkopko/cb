# Project Brief: cb

## Project Overview

The cb (Continuous Buffer) project is a foundational library that implements a power-of-2 sized ring buffer and related data structures designed to support wait-free garbage collection, persistent data structures, and a novel memory management approach for language runtimes.

## Core Objective

To create a robust, efficient implementation of memory management primitives that enable:
1. O(1) garbage collection without pausing the main program thread
2. Persistent data structures with efficient path-copying semantics
3. A simple and elegant memory model that eliminates traditional distinctions between stack and heap allocation

## Key Research Questions

1. Can we implement a truly wait-free garbage collection mechanism using the tri-partite memory model?
2. What are the performance characteristics and tradeoffs of using a handle-based reference system instead of direct pointers?
3. Can persistent data structures provide both performance benefits and more intuitive programming models?
4. How effectively can we eliminate memory fragmentation through the continuous buffer approach?

## Technical Approach

- Implement a power-of-2 sized ring buffer (`struct cb`) with efficient cursor-based allocation
- Use "magic ring buffer" mapping to avoid bounds checking and split operations
- Implement serial number arithmetic for reliable offset comparisons
- Create a tri-partite memory model with regions A, B, and C for concurrent GC
- Develop an O(log32 n) object ID to offset mapping (`structmap_amt`)
- Build persistent data structures (red-black trees, hash maps) that work within this model

## Performance Goals

- O(1) allocation via bump-pointer style cursor advancement
- O(1) garbage collection by making collection work concurrent
- O(log32 n) object dereferencing through structmap lookups
- Efficient memory utilization through compaction and defragmentation
- Predictable, non-pausing memory management for real-time applications

## Project Scope

- This project is a library that provides memory management primitives
- It serves as the foundation for the klox project's O(1) GC implementation
- The project includes the core continuous buffer implementation, structmap, and supporting data structures
- Testing infrastructure to verify correctness of all components

## Success Criteria

- Robust implementation that passes all test cases
- Demonstrated O(1) garbage collection capability
- Effective memory utilization with minimal fragmentation
- Clear APIs for integration with language runtimes
- Well-documented implementation with thorough explanations of the novel concepts
