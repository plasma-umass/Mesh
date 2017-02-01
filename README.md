Meshing: Compaction without Relocation
======================================

Implementation Overview
-----------------------

Mesh is built on [Heap Layers](http://heaplayers.org/), an
infrastructure for building high performance memory allocators in C++
(see
[paper](https://people.cs.umass.edu/~emery/pubs/berger-pldi2001.pdf)
for details.

The main file that defines the core of the library is [libmesh.cc](src/libmesh.cc).

TODO
----

- modify StrictSegHeap to keep track of hashmap of miniheaps
- wire up delete
