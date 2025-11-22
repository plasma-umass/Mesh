# Mesh: Compacting Memory Management for C/C++ Applications

## Overview

Mesh is a groundbreaking memory allocator that performs **compaction without relocation** for unmodified C/C++ applications. Published at PLDI 2019 by Bobby Powers, David Tench, Emery D. Berger, and Andrew McGregor from the University of Massachusetts Amherst, Mesh solves the long-standing problem of memory fragmentation in unmanaged languages.

### Key Innovation: Compaction Without Moving Objects

Unlike garbage-collected languages where objects can be relocated and pointers updated, C/C++ applications expose raw memory addresses that can be manipulated arbitrarily (hidden in integers, stored to disk, used in pointer arithmetic). This makes traditional compaction impossible. Mesh's breakthrough is achieving compaction **without changing object addresses** through a technique called "meshing."

## Core Concepts

### 1. Memory Fragmentation Problem

#### The Challenge
- **Catastrophic fragmentation**: Robson showed that all traditional allocators can suffer memory consumption up to O(log(max_size/min_size)) times the actual requirement
- Example: An application allocating 16-byte and 128KB objects could consume 13× more memory than needed
- Real-world impact:
  - 99% of Chrome crashes on low-end Android devices are due to out-of-memory conditions
  - Firefox underwent a 5-year effort to reduce memory footprint
  - Redis implements custom "active defragmentation" to combat fragmentation

#### Why Traditional Solutions Don't Work for C/C++
- Objects cannot be safely relocated because:
  - Programs can stash addresses in integers
  - Store flags in low bits of aligned addresses
  - Perform arithmetic on addresses
  - Store addresses to disk and reload them later
  - There's no way to find and update all references

### 2. The Meshing Technique

Meshing consolidates the contents of multiple partially-filled pages onto a single physical page while keeping multiple virtual pages pointing to it. This is the core innovation that enables compaction without relocation.

#### How Meshing Works
1. **Find meshable pages**: Two pages are meshable if their allocated objects don't overlap at the same offsets
2. **Copy and remap**: Copy contents from one page to another, then update virtual-to-physical mappings so both virtual pages point to the same physical page
3. **Release memory**: Return the now-unused physical page to the OS

#### Key Properties
- **Virtual addresses unchanged**: Objects remain at the same virtual addresses
- **Physical memory consolidated**: Multiple sparse virtual pages share dense physical pages
- **Transparent to applications**: No code changes or recompilation required

### 3. Randomized Allocation Strategy

To ensure pages are likely to be meshable, Mesh uses randomized allocation instead of traditional sequential allocation.

#### The Problem with Sequential Allocation
If objects are allocated sequentially (bump-pointer style), pages are likely to have objects at the same offsets, preventing meshing. In the worst case, every page could have exactly one object at the same offset, making meshing impossible.

#### Mesh's Solution: Shuffle Vectors
- **Random placement**: Objects are allocated uniformly at random across available offsets in a span
- **Mathematical guarantee**: Probability that all objects occupy the same offset = (1/b)^(n-1) where b = objects per span, n = number of spans
- **Example**: With 64 spans of 16-byte objects in 4K pages (256 objects/page), the probability of no meshing = 10^-152

### 4. Efficient Meshing Search Algorithm: SplitMesher

Finding meshable pages efficiently is critical for runtime performance.

#### The Algorithm
```
SplitMesher(S, t):
1. Split span list S into two halves: Sl and Sr
2. For each span in Sl:
   - Probe up to t spans from Sr for meshing opportunities
   - If meshable pair found, mesh them and remove from lists
3. Parameter t controls time/quality tradeoff (default: 64)
```

#### Key Properties
- **Probabilistic guarantees**: Finds approximation within factor of 1/2 of optimal with high probability
- **Efficient runtime**: O(n/q) where n = number of spans, q = probability two spans mesh
- **Practical effectiveness**: t=64 balances runtime overhead and meshing quality

## Architecture and Implementation

### System Architecture

Mesh is implemented as a drop-in replacement for malloc/free, requiring no source code changes. It can be used via:
- **Static linking**: Compile with `-lmesh`
- **Dynamic loading**: Set `LD_PRELOAD=libmesh.so` (Linux) or `DYLD_INSERT_LIBRARIES` (macOS)

### Core Components

#### 1. MiniHeaps (`mini_heap.h`)
- **Purpose**: Manage physical spans of memory with metadata
- **Key features**:
  - Allocation bitmap tracking occupied/free slots
  - Support for meshing multiple virtual spans to one physical span
  - Atomic operations for thread-safe non-local frees
  - Size classes from segregated-fit allocation
- **States**:
  - **Attached**: Owned by a thread-local heap for allocation
  - **Detached**: Owned by global heap, available for meshing

#### 2. Shuffle Vectors (`shuffle_vector.h`)
- **Purpose**: Enable fast randomized allocation with low overhead
- **Design**: Fixed-size array of available offsets in random order
- **Operations**:
  - **Allocation**: Pop next offset from vector (O(1))
  - **Deallocation**: Push freed offset and perform one Fisher-Yates shuffle iteration
- **Space efficiency**:
  - Only 1 byte per offset (max 256 objects per span)
  - One shuffle vector per attached MiniHeap per thread
  - Total overhead: ~2.8KB per thread for 24 size classes
- **Thread-local**: No synchronization needed, unlike bitmaps

#### 3. Thread Local Heaps (`thread_local_heap.h`)
- **Purpose**: Fast, lock-free allocation for small objects
- **Components**:
  - Shuffle vectors for each size class
  - Reference to global heap for refills
  - Thread-local PRNG for randomization
- **Allocation fast path**:
  1. Pop from shuffle vector if available
  2. Refill from attached MiniHeaps if vector empty
  3. Request new MiniHeap from global heap if needed

#### 4. Global Heap (`global_heap.h`)
- **Purpose**: Coordinate meshing, manage MiniHeaps, handle large allocations
- **Responsibilities**:
  - Allocate/deallocate MiniHeaps for thread-local heaps
  - Perform meshing operations across all size classes
  - Handle non-local frees from other threads
  - Manage large (>16KB) allocations directly
- **Meshing coordination**:
  - Rate-limited (default: max once per 100ms)
  - Skipped if last mesh freed <1MB
  - Concurrent with normal allocation

#### 5. Meshable Arena (`meshable_arena.h`)
- **Purpose**: Manage virtual and physical memory for meshing
- **Key innovation**: Uses file-backed shared mappings instead of anonymous memory
  - Created via `memfd_create()` (memory-only file)
  - Allows multiple virtual addresses to map same physical offset
  - Enables atomic page table updates via `mmap()`
- **Page management**:
  - Tracks ownership (MiniHeapID) for each page
  - Maintains bins of free/used pages by size
  - Implements `scavenge()` to return pages to OS

### Meshing Implementation Details

#### Concurrent Meshing
Meshing runs concurrently with application threads, maintaining two invariants:
1. **Read correctness**: Objects being relocated are always readable
2. **Write safety**: Objects are never written during relocation

#### Write Barrier Implementation
- **Page protection**: Before copying, source pages marked read-only via `mprotect()`
- **Trap handler**: Segfault handler waits for meshing to complete
- **Atomic remapping**: After copy, source pages remapped read/write
- **Zero stop-the-world**: No global synchronization required

#### Meshing Algorithm (`meshing.h`, `global_heap.cc`)
```cpp
// Core meshing check: do bitmaps overlap?
bool bitmapsMeshable(bitmap1, bitmap2):
  return (bitmap1 & bitmap2) == 0

// Main orchestration
meshAllSizeClassesLocked():
  1. Scavenge freed pages
  2. For each size class:
     - Flush thread-local free memory
     - Run shiftedSplitting algorithm
     - Consolidate meshable pairs
  3. Update statistics and page tables
```

### Memory Layout and Size Classes

#### Size Classes
- **Small objects**:
  - Same size classes as jemalloc for ≤1024 bytes
  - Power-of-two classes for 1024-16384 bytes
  - Reduces internal fragmentation from rounding
- **Large objects** (>16KB):
  - Page-aligned, individually managed
  - Not considered for meshing
  - Directly freed to OS when deallocated

#### Span Management
- **Span**: Contiguous run of pages containing same-sized objects
- **Occupancy tracking**: Bins organized by fullness (75-99%, 50-74%, etc.)
- **Random selection**: Global heap randomly selects from bins for reuse
- **Meshing candidates**: Only spans with occupancy below threshold (configurable)

## Theoretical Analysis and Guarantees

### Formal Problem Definition
Given n binary strings of length b (representing allocation bitmaps), find a meshing that releases the maximum number of strings. This reduces to:
- **Graph representation**: Nodes = strings, edges = meshable pairs
- **Optimization goal**: Minimum clique cover (partition into fewest cliques)
- **Complexity**: NP-hard in general, but polynomial for constant string length

### Probabilistic Guarantees

#### Mesh Breaks Robson Bounds
- **Traditional allocators**: Worst-case fragmentation of O(log(max_size/min_size))
- **Mesh**: With high probability, avoids catastrophic fragmentation
- **Key insight**: Meshing can redistribute memory between size classes

#### SplitMesher Analysis
Given:
- n spans to mesh
- q = global probability two spans mesh
- t = probe limit parameter (default: 64)

Results:
- **Quality**: Finds ≥n(1-e^(-2k))/4 meshes with high probability where k=t×q
- **Runtime**: O(n×k/q) probes in worst case
- **Practical impact**: For reasonable q values, finds near-optimal meshing quickly

### Randomization Analysis

#### Why Randomization is Essential
Without randomization, regular allocation patterns can prevent meshing entirely. Experiments show:
- **No randomization**: Only 3% heap reduction, 4% overhead
- **With randomization**: 19% heap reduction, worth the 10.7% overhead

#### Mathematical Properties
- **Independence**: Edges in meshing graph are not fully independent (3-wise dependence)
- **Triangle rarity**: P(3 strings all mesh) << P(pairwise meshing)^3
- **Implication**: Can focus on finding pairs (matching) vs. larger cliques

## Performance Characteristics

### Memory Savings (from paper evaluation)

#### Firefox (Speedometer 2.0 benchmark)
- **16% RSS reduction** vs. bundled jemalloc
- 530MB with Mesh vs. 632MB with mozjemalloc
- <1% performance impact on benchmark score
- Consistent lower memory throughout execution

#### Redis (with heavy fragmentation workload)
- **39% RSS reduction** automatically
- Matches Redis's custom "active defragmentation" savings
- 5.5× faster than Redis's built-in defragmentation
- No application modifications required

#### SPEC CPU2006
- Modest 2.4% average reduction (not allocation-intensive)
- 15% reduction for perlbench (allocation-intensive)
- 0.7% geometric mean runtime overhead

### Runtime Overhead

#### Meshing Costs
- **Frequency**: Rate-limited to once per 100ms by default
- **Duration**: Average 0.2ms, max 7.5ms (Firefox)
- **Concurrent execution**: No stop-the-world pauses
- **Adaptive**: Skips meshing if ineffective (<1MB freed)

#### Allocation Performance
- **Fast path**: Thread-local, lock-free via shuffle vectors
- **Random allocation**: ~2-3 additional instructions vs. bump pointer
- **Cache behavior**: Comparable to modern allocators
- **Scalability**: Thread-local heaps minimize contention

### Space Overhead

#### Per-Thread
- **Shuffle vectors**: ~2.8KB total (24 size classes × ~120 bytes)
- **Thread-local metadata**: <1KB

#### Global
- **MiniHeap metadata**: 64 bytes per MiniHeap
- **Bitmap overhead**: 1 bit per object slot
- **Page tables**: Standard virtual memory overhead

## Configuration and Tuning

### Key Parameters

#### Meshing Parameters
- **Mesh rate**: `mesh.check_period` (default: 100ms)
- **Effectiveness threshold**: Minimum bytes to free (default: 1MB)
- **Probe limit (t)**: Balances quality vs. runtime (default: 64)
- **Max meshes per iteration**: Prevents excessive meshing time

#### Allocation Parameters
- **Shuffle on allocation**: Always enabled for randomization
- **Shuffle on free**: Optional additional randomization
- **Occupancy cutoff**: Max fullness for meshing candidates (default: configurable)

### Usage Modes

#### Default Mode
Full meshing with randomization enabled. Best for:
- Long-running applications
- Memory-constrained environments
- Applications with fragmentation issues

#### No-Meshing Mode
Randomized allocation only. Useful for:
- Debugging allocation patterns
- Performance comparison
- Applications with regular allocation patterns

#### Compatibility Notes
- **Transparent huge pages**: Should be disabled (conflicts with 4KB page granularity)
- **Direct huge page allocation**: Still supported via mmap interfaces
- **Security features**: Compatible with ASLR, DEP, etc.

## Limitations and Considerations

### When Mesh is Most Effective
- **High fragmentation**: Many partially-filled pages
- **Long-running applications**: Time for fragmentation to develop
- **Mixed allocation sizes**: Benefits from cross-size-class redistribution
- **Memory-constrained environments**: Where savings matter most

### When Mesh May Be Less Effective
- **Sequential allocation patterns**: Without enough entropy for meshing
- **Very large allocations**: >16KB objects not meshed
- **Short-lived programs**: Insufficient time for meshing benefits
- **Full pages**: Nothing to compact when pages are dense

### Overhead Considerations
- **Virtual address space**: 2× consumption in worst case (unmeshed pages)
- **TLB pressure**: Multiple virtual pages per physical page
- **Page faults**: Initial access to meshed pages
- **Memory bandwidth**: Copying during mesh operations

## Implementation Status

### Platform Support
- **Linux**: Full support (primary platform)
- **macOS**: Full support
- **64-bit only**: Current implementation requires 64-bit address space

### Integration
- **Drop-in replacement**: No code changes required
- **Standard API**: Full malloc/free/realloc/memalign support
- **Statistics**: mallctl API for runtime introspection
- **Open source**: Apache 2.0 license

## Key Insights and Innovations

### Revolutionary Concepts
1. **Compaction without relocation**: Previously thought impossible for C/C++
2. **Virtual memory as abstraction layer**: Leverages MMU for pointer stability
3. **Randomization for meshability**: Probabilistic approach to fragmentation

### Theoretical Contributions
1. **Breaks Robson bounds**: First allocator to provably avoid worst-case fragmentation
2. **Cross-size-class redistribution**: Unique ability to move memory between classes
3. **Formal analysis**: Rigorous probabilistic guarantees on effectiveness

### Practical Impact
1. **Automatic memory savings**: No application changes required
2. **Compatible with existing code**: True drop-in replacement
3. **Solves real problems**: Demonstrated savings in Firefox and Redis

## Summary

Mesh represents a breakthrough in memory management for unmanaged languages, achieving what was previously thought impossible: automatic compaction for C/C++ applications. Through the novel combination of meshing (virtual page remapping), randomized allocation, and efficient mesh search algorithms, Mesh provides:

- **Significant memory savings**: 16-39% reduction in real applications
- **Theoretical guarantees**: Breaks classical fragmentation bounds
- **Practical deployment**: Drop-in replacement requiring no code changes
- **Low overhead**: <1% performance impact in most cases

The key innovation—compaction without relocation—opens new possibilities for memory management in systems programming, potentially influencing future language runtimes and operating system designs.