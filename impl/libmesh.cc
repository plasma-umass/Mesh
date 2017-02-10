// Copyright 2016 University of Massachusetts, Amherst

#include <stdio.h>   // for sprintf
#include <stdlib.h>  // for abort
#include <unistd.h>  // for write
#include <cstdarg>   // for va_start + friends
#include <cstddef>   // for size_t
#include <new>       // for operator new

#include "file-backed-mmap.h"
#include "meshingheap.h"

#include "heaplayers.h"

#include "wrappers/gnuwrapper.cpp"

// The top heap provides memory to back spans managed by MiniHeaps.
class TopHeap : public ExactlyOneHeap<mesh::FileBackedMmapHeap> {
private:
  typedef ExactlyOneHeap<mesh::FileBackedMmapHeap> SuperHeap;
public:
  void mesh(void *keep, void *remove) {
    getSuperHeap().internalMesh(keep, remove);
  }
};
// The top big heap is called to handle malloc requests for large
// objects.  We define a separate class to handle these to segregate
// bookkeeping for large malloc requests from the ones used to back
// spans (which are allocated from TopHeap)
class TopBigHeap : public ExactlyOneHeap<mesh::MmapHeap> {};

// fewer buckets than regular KingsleyHeap (to ensure multiple objects
// fit in the 128Kb spans used by MiniHeaps).
class BottomHeap : public mesh::MeshingHeap<11, mesh::size2Class, mesh::class2Size, 20, TopHeap, TopBigHeap> {};

// TODO: remove the LockedHeap here and use a per-thread BottomHeap
class CustomHeap : public ANSIWrapper<LockedHeap<PosixLockType, BottomHeap>> {};

inline static CustomHeap *getCustomHeap(void) {
  static char buf[sizeof(CustomHeap)];
  static CustomHeap *heap = new (buf) CustomHeap();

  return heap;
}

extern "C" {
void *xxmalloc(size_t sz) {
  return getCustomHeap()->malloc(sz);
}

void xxfree(void *ptr) {
  getCustomHeap()->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return getCustomHeap()->getSize(ptr);
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking
void xxmalloc_lock(void) {
  getCustomHeap()->lock();
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking
void xxmalloc_unlock(void) {
  getCustomHeap()->unlock();
}
}
