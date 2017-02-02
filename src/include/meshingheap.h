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
          constexpr int (*getSizeClass)(const size_t),
          size_t (*getClassMaxSize)(const int),
          typename SuperHeap,
          typename BigHeap>
class MeshingHeap {
private:
  typedef MiniHeapBase<SuperHeap, internal::Heap> MiniHeap;

public:
  enum { Alignment = 16 };  // FIXME

  MeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _bigheap(), _littleheaps(), _miniheaps() {
    static_assert(getClassMaxSize(NumBins-1) == 16384, "expected 16k max object size");
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }
  }

  inline void *malloc(size_t sz) {
    const int sizeClass = getSizeClass(sz);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    d_assert_msg(sz <= sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", sz, sizeMax, sizeClass);

    void *ptr = nullptr;

    if (sizeMax <= _maxObjectSize) {
      assert(sizeClass >= 0);
      assert(sizeClass < NumBins);

      if (_current[sizeClass] == nullptr) {
        void *buf = internal::Heap().malloc(sizeof(MiniHeap));
        d_assert(buf != nullptr);

        debug("\t%zu // %zu (%zu)", sizeClass, sizeMax, sz);
        MiniHeap *mh = new (buf) MiniHeap(sizeMax);
        d_assert(!mh->isFull());

        _littleheaps[sizeClass].push_back(mh);
        _miniheaps[mh->getSpanStart()] = mh;
        _current[sizeClass] = mh;

        d_assert(_littleheaps[sizeClass].size() > 0);
        d_assert(_littleheaps[sizeClass][_littleheaps[sizeClass].size()-1] == mh);
        d_assert(_miniheaps[mh->getSpanStart()] == mh);
        d_assert(_current[sizeClass] == mh);
      }

      d_assert(_current[sizeClass] != nullptr);
      MiniHeap *mh = _current[sizeClass];

      ptr = mh->malloc(sizeMax);
      if (mh->isFull()) {
        debug("\tzu // %zu FULL (%p)", sizeClass, sizeMax, mh);
        mh->setDone();
        _current[sizeClass] = nullptr;
      }
    } else {
      ptr = _bigheap.malloc(sz);
      // debug("BigAlloc(%zu) from mmap                                                     \t%p-%p", sz, ptr, reinterpret_cast<uintptr_t>(ptr)+sz);
    }

    return ptr;
  }

  static inline uintptr_t getSpanStart(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr) & ~(MiniHeap::span_size - 1);
  }

  inline MiniHeap *miniheapFor(void *ptr) {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    auto const it = greatest_less(_miniheaps, ptrval);
    if (it != _miniheaps.end()) {
      auto candidate = it->second;
      d_assert(it->second != nullptr);
      const auto spanStart = candidate->getSpanStart();
      if (spanStart <= ptrval && ptrval < spanStart + MiniHeap::span_size)
        return candidate;
    }

    return nullptr;
  }

  inline void free(void *ptr) {
    auto mh = miniheapFor(ptr);
    if (mh) {
      mh->free(ptr);
    } else {
      // debug("freeing big %p, know about small:", ptr);
      // for(auto i : _miniheaps) {
      //   debug("\t%p\n", i.first);
      // }
      return _bigheap.free(ptr);
    }
  }

  inline size_t getSize(void *ptr) {
    if (ptr == nullptr)
      return 0;

    auto mh = miniheapFor(ptr);
    if (mh) {
      return mh->getSize(ptr);
    } else {
      return _bigheap.getSize(ptr);
    }
  }

private:
  const size_t _maxObjectSize;

  BigHeap _bigheap;
  MiniHeap *_current[NumBins];

  internal::vector<MiniHeap *> _littleheaps[NumBins];
  internal::map<uintptr_t, MiniHeap *> _miniheaps;
};
}

#endif  // MESH_MESHINGHEAP_H
