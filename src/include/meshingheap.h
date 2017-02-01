// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_MESHINGHEAP_H
#define MESH_MESHINGHEAP_H

#include "internal.h"
#include "heaplayers.h"
#include "miniheap.h"

using namespace HL;

namespace mesh {

template <int NumBins,
          int (*getSizeClass)(const size_t),
          size_t (*getClassMaxSize)(const int),
          typename SuperHeap,
          typename BigHeap>
class MeshingHeap {
private:
  typedef MiniHeap<SuperHeap, internal::Heap> MiniHeap;

public:
  enum { Alignment = 16 };  // FIXME

  MeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _bigheap(), _current(nullptr), _miniheaps() {
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");
    for (size_t i = 0; i < NumBins; i++) {
      _littleheap[i] = nullptr;
    }
  }

  inline void *malloc(size_t sz) {
    static thread_local int inMalloc = 0;
    // guard against recursive malloc
    if (inMalloc != 0) {
      debug("recursive malloc detected\n");
      abort();
    }
    inMalloc = 1;

    if (unlikely(_current == nullptr)) {
      void *buf = internal::Heap().malloc(sizeof(MiniHeap));
      if (!buf)
        abort();
      _current = new (buf) MiniHeap(sz);
      assert(!_current->isFull());
    }

    void *ptr = _current->malloc(sz);
    if (_current->isFull()) {
      _current = nullptr;
    }

    inMalloc = 0;
    return ptr;
  }

  inline void free(void *ptr) {
    // FIXME: check if ptr is in current, if so free there, otherwise check all other miniheaps
    // this needs to take into account threads + locks, maybe
  }

  inline size_t getSize(void *ptr) {
    if (_current == nullptr)
      return 0;
    return _current->getSize(ptr);
  }

private:
  const size_t _maxObjectSize;

  BigHeap _bigheap;
  MiniHeap *_littleheap[NumBins];

  MiniHeap *_current;

  internal::unordered_map<uintptr_t, MiniHeap *> _miniheaps;
};
}

#endif  // MESH_MESHINGHEAP_H
