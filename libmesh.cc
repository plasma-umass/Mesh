// Copyright 2016 University of Massachusetts, Amherst

#include <stdlib.h>

#include <atomic>

#define MALLOC_TRACE 0

#include "heaplayers.h"
#include "wrappers/gnuwrapper.cpp"

using namespace HL;

template <class Mmap>
class WrappedMmapAlloc : public Mmap {
 public:
  enum { Alignment = Mmap::Alignment };

  WrappedMmapAlloc() {
    static char buf[2048];
    auto len = sprintf(buf, "Mmap::Alignment: %d\n", Alignment);
    write(1, buf, len);
  }

  void *malloc(size_t sz) {
    return Mmap::map(sz);
  }

  void free(void *) {
    // This should never get called.
    abort();
  }
};

class TopHeap : public ExactlyOneHeap<SizeHeap<BumpAlloc<1048576, WrappedMmapAlloc<MmapWrapper>, 16>>> {
  typedef ExactlyOneHeap<SizeHeap<BumpAlloc<1048576, WrappedMmapAlloc<MmapWrapper>, 16>>> Super;
};

// no freelists
class MeshHeap : public ANSIWrapper<KingsleyHeap<TopHeap, TopHeap>> {};

inline static MeshHeap *getMeshHeap(void) {
  static char mesh_buf[sizeof(MeshHeap)];
  static MeshHeap *mesh = new (mesh_buf) MeshHeap();
  return mesh;
}

extern "C" {
void *xxmalloc(size_t sz) {
  return getMeshHeap()->malloc(sz);
}

void xxfree(void *ptr) {
  getMeshHeap()->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return getMeshHeap()->getSize(ptr);
}

void xxmalloc_lock() {
}

void xxmalloc_unlock() {
}
}
