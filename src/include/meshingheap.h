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

  MeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _bigheap(), _littleheaps(), _miniheaps() {
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");
  }

  inline void *malloc(size_t sz) {
    const int sizeClass = getSizeClass(sz);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    d_assert_msg(sz <= sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", sz, sizeMax, sizeClass);

    void *ptr = nullptr;

    if (likely(sz <= _maxObjectSize)) {
      assert(sizeClass >= 0);
      assert(sizeClass < NumBins);

      if (unlikely(_current[sizeClass] == nullptr)) {
        void *buf = internal::Heap().malloc(sizeof(MiniHeap));
        d_assert(buf != nullptr);

        auto mh = new (buf) MiniHeap(sz);
        d_assert(!mh->isFull());

        _current[sizeClass] = mh;
        _littleheaps[sizeClass].push_back(mh);
        _miniheaps[mh->getSpanStart()] = mh;
      }

      auto mh = _current[sizeClass];

      ptr = mh->malloc(sz);
      if (mh->isFull()) {
        _current[sizeClass] = nullptr;
      }
    } else {
      ptr = _bigheap.malloc(sz);
    }

    return ptr;
  }

  static inline uintptr_t getSpanStart(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr) & ~(MiniHeap::span_size - 1);
  }

  inline void free(void *ptr) {
    auto spanStart = getSpanStart(ptr);

    auto it = _miniheaps.find(spanStart);
    if (it != _miniheaps.end()) {
      return it->second->free(ptr);
    } else {
      return _bigheap.free(ptr);
    }
  }

  inline size_t getSize(void *ptr) {
    if (ptr == nullptr)
      return 0;

    auto spanStart = getSpanStart(ptr);

    auto it = _miniheaps.find(spanStart);
    if (it != _miniheaps.end()) {
      return it->second->getSize(ptr);
    } else {
      return _bigheap.getSize(ptr);
    }
  }

private:
  const size_t _maxObjectSize;

  BigHeap _bigheap;
  MiniHeap *_current[NumBins];

  internal::vector<MiniHeap *> _littleheaps[NumBins];
  internal::unordered_map<uintptr_t, MiniHeap *> _miniheaps;
};
}

#endif  // MESH_MESHINGHEAP_H
