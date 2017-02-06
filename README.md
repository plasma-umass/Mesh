Meshing: Compaction without Relocation
======================================

Implementation Overview
-----------------------

Mesh is built on [Heap Layers](http://heaplayers.org/), an
infrastructure for building high performance memory allocators in C++
(see
[paper](https://people.cs.umass.edu/~emery/pubs/berger-pldi2001.pdf)
for details.

The entry point of the library is [`libmesh.cc`](src/libmesh.cc).
This file is where `malloc`, `free` and the instantiations of the
Heaps used for allocating program and mesh-internal memory live.


BUILDING
--------

Running `make` will build the library and run a simple test of
executing `git status` with `libmesh` as the allocator:

```
$ make
```


TODO
----

- Fix issue with `FileBackedMmapHeap` on fork (shared pages between processes) 
