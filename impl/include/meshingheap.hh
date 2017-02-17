// -*- mode: c++ -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__MESHINGHEAP_H
#define MESH__MESHINGHEAP_H

#include <algorithm>

#include "heaplayers.h"

#include "internal.hh"
#include "meshing.hh"
#include "miniheap.hh"

using namespace HL;

namespace mesh {

template <int NumBins,
          int (*getSizeClass)(const size_t),
          size_t (*getClassMaxSize)(const int),
          int MeshPeriod, // perform meshing on average once every MeshPeriod frees
          typename SuperHeap,
          typename BigHeap>
class MeshingHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshingHeap);
  typedef MiniHeapBase<SuperHeap, internal::Heap> MiniHeap;

public:
  enum { Alignment = 16 };

  MeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _prng(internal::seed()) {
    static_assert(getClassMaxSize(NumBins - 1) == 16384, "expected 16k max object size");
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }

    resetNextMeshCheck();
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

      // debug("\t%zu // %zu (%zu)", sizeClass, sizeMax, sz);
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

  inline MiniHeap *miniheapFor(void *const ptr) {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    auto it = greatest_leq(_miniheaps, ptrval);
    if (likely(it != _miniheaps.end())) {
      auto candidate = it->second;
      if (likely(candidate->contains(ptr)))
        return candidate;
    }

    return nullptr;
  }

  inline void free(void *ptr) {
    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      mh->free(ptr);
      if (unlikely(mh->isDone() && mh->isEmpty())) {
        // FIXME: free up heap metadata
        _miniheaps.erase(mh->getSpanStart());
      } else if (unlikely(shouldMesh())) {
        meshAllSizeClasses();
      }
    } else {
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

protected:
  inline void resetNextMeshCheck() {
    uniform_int_distribution<size_t> distribution(1, MeshPeriod);
    _nextMeshCheck = distribution(_prng);
  }

  inline bool shouldMesh() {
    _nextMeshCheck--;
    bool shouldMesh = _nextMeshCheck == 0;
    if (unlikely(shouldMesh))
      resetNextMeshCheck();

    return shouldMesh;
  }

  inline void meshAllSizeClasses() {
    for (auto heaps : _littleheaps) {
      auto begin = heaps.begin();
      auto end = heaps.end();
      // if the current heap (the one we are allocating out of) isn't
      // done, exclude it from meshing
      if (heaps.size() > 1 && !heaps.back()->isDone())
        --end;
      if (std::distance(begin, end) > 1) {
        // chose a random permutation of same-sized MiniHeaps
        std::shuffle(begin, end, _prng);
        for (auto it1 = begin, it2 = ++begin; it2 != end; ++it1, ++it2) {
          MiniHeap *h1 = *it1;
          MiniHeap *h2 = *it2;

          if (h1->isDone() || h2->isDone())
            continue;

          const auto len = h1->bitmap().wordCount();
          const auto bitmap1 = h1->bitmap().bitmap();
          const auto bitmap2 = h2->bitmap().bitmap();
          d_assert(len == h2->bitmap().wordCount());

          if (mesh::bitmapsMeshable(bitmap1, bitmap2, len)) {
            debug("----\n2 MESHABLE HEAPS!\n");
            MiniHeap::mesh(h1, h2);

            // h1->dumpDebug();
            // h2->dumpDebug();
            // debug("----\n");

            // TODO: merge the two heaps
          }
        }
      }
    }
  }

  const size_t _maxObjectSize;
  size_t _nextMeshCheck;

  BigHeap _bigheap{};
  MiniHeap *_current[NumBins];

  mt19937_64 _prng;

  internal::vector<MiniHeap *> _littleheaps[NumBins]{};
  internal::map<uintptr_t, MiniHeap *> _miniheaps{};
};
}

#endif  // MESH__MESHINGHEAP_H
