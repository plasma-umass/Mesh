Mesh: Compacting Memory Management for C/C++
============================================

Mesh is a drop in replacement for
[malloc(3)](http://man7.org/linux/man-pages/man3/malloc.3.html)
that can transparently recover from memory fragmentation without any changes
to application code.

Mesh is described in detail in a [paper (PDF)](https://github.com/plasma-umass/Mesh/raw/master/mesh-pldi19-powers.pdf) that appeared at PLDI 2019.

Or watch this talk by Bobby Powers at Strange Loop:

[![Compacting the Uncompactable](https://img.youtube.com/vi/c1UBJbfR-H0/0.jpg)](https://www.youtube.com/watch?v=c1UBJbfR-H0)

Mesh runs on Linux and macOS.  Windows is a work in progress.

Mesh uses [bazel](https://bazel.build/) as a build system, but wraps it in a Makefile, and has no runtime dependencies
other than libc:

```
$ git clone https://github.com/plasma-umass/mesh
$ cd mesh
$ make; sudo make install
# example: run git with mesh as its allocator:
$ LD_PRELOAD=libmesh.so git status
```

Please open an issue if you have questions (or issues)!


But will it blend?
------------------

If you run a program linked against mesh (or with Mesh `LD_PRELOAD`ed), setting the variable `MALLOCSTATS=1` will instruct mesh to print a summary at exit:

```
$ MALLOCSTATS=1 ./bin/redis-server-mesh ./redis.conf
25216:C 11 Mar 20:27:12.050 # oO0OoO0OoO0Oo Redis is starting oO0OoO0OoO0Oo
25216:C 11 Mar 20:27:12.050 # Redis version=4.0.2, bits=64, commit=dfe0d212, modified=0, pid=25216, just started
25216:C 11 Mar 20:27:12.050 # Configuration loaded
[...]
^C25216:signal-handler (1583983641) Received SIGINT scheduling shutdown...
25216:M 11 Mar 20:27:21.945 # User requested shutdown...
25216:M 11 Mar 20:27:21.945 * Removing the pid file.
25216:M 11 Mar 20:27:21.945 * Removing the unix socket file.
25216:M 11 Mar 20:27:21.945 # Redis is now ready to exit, bye bye...
MESH COUNT:         25918
Meshed MB (total):  101.2
Meshed pages HWM:   25918
Meshed MB HWM:      101.2
MH Alloc Count:     56775
MH Free  Count:     17
MH High Water Mark: 82687
```

Not all workloads experience fragmentation, so its possible that Mesh will have a small 'Meshed MB (total' number!


Implementation Overview
-----------------------

Mesh is built on [Heap Layers](http://heaplayers.org/), an
infrastructure for building high performance memory allocators in C++
(see the
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
