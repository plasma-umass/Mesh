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
  DISALLOW_COPY_AND_ASSIGN(MeshingHeap);
  typedef MiniHeapBase<SuperHeap, internal::Heap> MiniHeap;

public:
  enum { Alignment = 16 };

  MeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _bigheap(), _littleheaps(), _miniheaps() {
    debug("MeshingHeap init");
    static_assert(getClassMaxSize(NumBins-1) == 16384, "expected 16k max object size");
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }
  }

  inline void *malloc(size_t sz) {
    const int sizeClass = getSizeClass(sz);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    // d_assert_msg(sz <= sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", sz, sizeMax, sizeClass);

    if (unlikely(sizeMax > _maxObjectSize))
      return _bigheap.malloc(sz);

    // d_assert(sizeMax <= _maxObjectSize);
    // d_assert(sizeClass >= 0);
    // d_assert(sizeClass < NumBins);

    if (unlikely(_current[sizeClass] == nullptr)) {
      void *buf = internal::Heap().malloc(sizeof(MiniHeap));
      if (buf == nullptr)
        abort();

      //debug("\t%zu // %zu (%zu)", sizeClass, sizeMax, sz);
      MiniHeap *mh = new (buf) MiniHeap(sizeMax);
      // d_assert(!mh->isFull());

      _littleheaps[sizeClass].push_back(mh);
      _miniheaps[mh->getSpanStart()] = mh;
      _current[sizeClass] = mh;

      // d_assert(_littleheaps[sizeClass].size() > 0);
      // d_assert(_littleheaps[sizeClass][_littleheaps[sizeClass].size()-1] == mh);
      // d_assert(_miniheaps[mh->getSpanStart()] == mh);
      // d_assert(_current[sizeClass] == mh);
    }

    MiniHeap *mh = _current[sizeClass];

    void *ptr = mh->malloc(sizeMax);
    if (unlikely(mh->isFull())) {
      mh->setDone();
      _current[sizeClass] = nullptr;
    }

    return ptr;
  }

  static inline uintptr_t getSpanStart(void *ptr) {
    return reinterpret_cast<uintptr_t>(ptr) & ~(MiniHeap::span_size - 1);
  }

  inline MiniHeap *miniheapFor(void *const ptr) {
    MiniHeap *mh = nullptr;
    for (auto i : _miniheaps) {
      if (i.first <= (uintptr_t)ptr && (uintptr_t)ptr < i.first + MiniHeap::span_size) {
        mh = i.second;
        d_assert(mh->contains(ptr));
      }
    }
    if (!mh)
      return nullptr;

    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    auto it = greatest_leq(_miniheaps, ptrval);
    if (it != _miniheaps.end()) {
      auto candidate = it->second;
      if (candidate->contains(ptr))
        return candidate;
    }

    if (mh) {
      debug("SHIT: %p in %p", ptr, mh);
      mh->dumpDebug();
      debug("FAILED:");
      const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
      auto it = greatest_leq(_miniheaps, ptrval);
      if (it != _miniheaps.end()) {
        auto candidate = it->second;
        d_assert(it->second != nullptr);
        const auto spanStart = candidate->getSpanStart();
        d_assert(spanStart == it->first);
        debug("MiniHeap(%p)\t\t%d && %d (%p <= %p < %p)", mh,
              spanStart <= ptrval,
              ptrval < (spanStart + MiniHeap::span_size));
        debug("\tmh == cand: %d", mh == candidate);
        debug("\tspanStart:  %p", candidate->getSpanStart());
        debug("\tspanStart:  %p", spanStart);
        debug("\tptr:        %p", ptr);
        debug("\tptrval:     %p", ptrval);
        debug("\tss+sz :     %p", spanStart + MiniHeap::span_size);
        mh->dumpDebug();
      }
    }
    return nullptr;
  }

  inline void free(void *ptr) {
    auto mh = miniheapFor(ptr);
    if (mh) {
      mh->free(ptr);
      if (mh->isDone() && mh->isEmpty()) {
        // FIXME: free up heap metadata
        _miniheaps.erase(reinterpret_cast<uintptr_t>(mh));
      }
    } else {
      // debug("freeing big %p, know about small:", ptr);
      // for(auto i : _miniheaps) {
      //   debug("\t%p\n", i.first);
      // }
       _bigheap.free(ptr);
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
