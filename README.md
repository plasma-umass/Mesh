Mesh: Compacting Memory Management for C/C++
============================================

Mesh is a drop in replacement for
[malloc(3)](http://man7.org/linux/man-pages/man3/malloc.3.html) that
compacts the heap without rewriting application pointers.

Mesh is described in an [academic paper (PDF)](https://github.com/plasma-umass/Mesh/raw/master/mesh-pldi19-powers.pdf) that appeared at PLDI 2019.

Or watch this talk by Bobby Powers at Strange Loop:

[![Compacting the Uncompactable](https://img.youtube.com/vi/c1UBJbfR-H0/0.jpg)](https://www.youtube.com/watch?v=c1UBJbfR-H0)

Mesh runs on Linux; macOS support should be considered alpha-quality,
and Windows is a work in progress.

Mesh has a standard C++ build process, and has no runtime dependencies
other than libc-related libs:

```
$ git clone --recurse-submodules https://github.com/plasma-umass/mesh
$ cd mesh
$ ./configure; make; sudo make install
# example: run git with mesh as its allocator:
$ LD_PRELOAD=libmesh.so git status
```

Please open an issue if you have questions (or issues)!


Implementation Overview
-----------------------

Mesh is built on [Heap Layers](http://heaplayers.org/), an
infrastructure for building high performance memory allocators in C++
(see
[paper](https://people.cs.umass.edu/~emery/pubs/berger-pldi2001.pdf)
for details.)

The entry point of the library is [`libmesh.cc`](src/libmesh.cc).
This file is where `malloc`, `free` and the instantiations of the
Heap used for allocating program memory lives.


DEFINITIONS
-----------

- **Page**: The smallest block of memory managed by the operating
  system, 4Kb on most architectures.  Memory given to the allocator by
  the operating system is always in multiples of the page size, and
  aligned to the page size.
- **Span**: A contiguous run of 1 or more pages.  It is often larger
  than the page size to account for large allocations and amortize the
  cost of heap metadata.
- **Arena**: A contiguous range of virtual address space we allocate
  out of.  All allocations returned by
  [`malloc(3)`](http://man7.org/linux/man-pages/man3/malloc.3.html)
  reside within the arena.
- [**GlobalHeap**](src/global_heap.h): The global heap carves out the
  Arena into Spans and performs meshing.
- [**MiniHeap**](src/mini_heap.h): Metadata for a Span -- at any time
  a live Span has a single MiniHeap owner.  For small objects,
  MiniHeaps have a bitmap to track whether an allocation is live or
  freed.
- [**ThreadLocalHeap**](src/thread_local_heap.h): A collections of
  MiniHeaps and a ShuffleVector so that most allocations and
  `free(3)`s can be fast and lock-free.
- [**ShuffleVector**](src/shuffle_vector.h): A novel data structure
  that enables randomized allocation with bump-pointer-like speed.
