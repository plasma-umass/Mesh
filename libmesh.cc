// Copyright 2016 University of Massachusetts, Amherst

#include <stdio.h>   // for sprintf
#include <stdlib.h>  // for abort
#include <unistd.h>  // for write
#include <cstddef>   // for size_t
#include <new>       // for operator new

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)

#define MALLOC_TRACE 0

#include "heaplayers.h"

#include "file-backed-mmap.h"

#include "wrappers/gnuwrapper.cpp"

using namespace HL;

typedef ExactlyOneHeap<LockedHeap<PosixLockType, FreelistHeap<BumpAlloc<16384, PrivateFileBackedMmapHeap>>>>
    InternalAlloc;

class TopHeap : public ExactlyOneHeap<FileBackedMmapHeap<InternalAlloc>> {
 public:
  typedef ExactlyOneHeap<FileBackedMmapHeap<InternalAlloc>> Super;
};

class MiniHeap {
  enum { Alignment = 16 };  // FIXME

  inline void *malloc(size_t sz) {
    return nullptr;
  }

  inline size_t getSize(void *ptr) {
    return 0;
  }

  inline void free(void *ptr) {
  }

  // rng
  // bitmap
};

template <typename SuperHeap, typename InternalAlloc>
class MeshingHeap : public SuperHeap {
 public:
  enum { Alignment = SuperHeap::Alignment };
};

// no freelists
class CustomHeap : public ANSIWrapper<KingsleyHeap<MeshingHeap<TopHeap, InternalAlloc>, TopHeap>> {};

inline static CustomHeap *getCustomHeap(void) {
  static char buf[sizeof(CustomHeap)];
  static CustomHeap *heap = new (buf) CustomHeap();
  return heap;
}

// want:

// MiniHeapManager<MiniHeap, TopHeap>

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

void xxmalloc_lock() {
}

void xxmalloc_unlock() {
}
}
