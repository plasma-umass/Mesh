// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__GLOBALMESHINGHEAP_H
#define MESH__GLOBALMESHINGHEAP_H

#include <algorithm>

#include "heaplayers.h"

#include "internal.h"
#include "meshable-arena.h"
#include "meshing.h"
#include "miniheap.h"

using namespace HL;

namespace mesh {

template <typename BigHeap,                      // for large allocations
          int NumBins,                           // number of size classes
          int (*getSizeClass)(const size_t),     // same as for local
          size_t (*getClassMaxSize)(const int),  // same as for local
          int MeshPeriod,                        // perform meshing on average once every MeshPeriod frees
          size_t MinStringLen = 8UL>
class GlobalMeshingHeap : public MeshableArena {
private:
  DISALLOW_COPY_AND_ASSIGN(GlobalMeshingHeap);
  typedef MeshableArena Super;

public:
  enum { Alignment = 16 };

  GlobalMeshingHeap() : _maxObjectSize(getClassMaxSize(NumBins - 1)), _prng(internal::seed()) {
    static_assert(getClassMaxSize(NumBins - 1) == 16384, "expected 16k max object size");
    static_assert(gcd<BigHeap::Alignment, Alignment>::value == Alignment, "expected BigHeap to have 16-byte alignment");

    resetNextMeshCheck();
  }

  inline MiniHeap *allocMiniheap(size_t objectSize) {
    d_assert(objectSize <= _maxObjectSize);

    const int sizeClass = getSizeClass(objectSize);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    d_assert_msg(objectSize == sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", objectSize, sizeMax,
                 sizeClass);
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < NumBins);

    void *buf = internal::Heap().malloc(sizeof(MiniHeap));
    if (unlikely(buf == nullptr))
      abort();

    // if we have objects bigger than the size of a page, allocate
    // multiple pages to amortize the cost of creating a
    // miniheap/globally locking the heap.
    size_t nObjects = max(MiniHeap::PageSize / sizeMax, MinStringLen);

    const size_t nPages = (sizeMax * nObjects + (MiniHeap::PageSize - 1)) / MiniHeap::PageSize;
    const size_t spanSize = MiniHeap::PageSize * nPages;
    d_assert(0 < spanSize);

    void *span = Super::malloc(spanSize);
    if (unlikely(span == nullptr))
      abort();

    MiniHeap *mh = new (buf) MiniHeap(span, spanSize, sizeMax);
    _littleheaps[sizeClass].push_back(mh);
    _miniheaps[mh->getSpanStart()] = mh;

    return mh;
  }

  inline void *malloc(size_t sz) {
    const int sizeClass = getSizeClass(sz);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    if (unlikely(sizeMax <= _maxObjectSize))
      abort();

    return _bigheap.malloc(sz);
  }

  inline MiniHeap *miniheapFor(void *const ptr) const {
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

  inline size_t getSize(void *ptr) const {
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

  // check for meshes in all size classes
  void meshAllSizeClasses() {
    internal::vector<internal::vector<MiniHeap *>> mergeSets;

    auto meshFound = function<void(internal::vector<MiniHeap *> &&)>(
        std::allocator_arg, internal::allocator,
        [&](internal::vector<MiniHeap *> &&mesh) { mergeSets.push_back(std::move(mesh)); });

    for (const auto &miniheaps : _littleheaps) {
      randomSort(_prng, miniheaps, meshFound);
    }

    if (mergeSets.size() == 0)
      return;

    debug("found something to merge!\n");

    internal::StopTheWorld();

    for (const auto &mergeSet : mergeSets) {
      d_assert(mergeSet.size() == 2);  // FIXME
      MiniHeap::mesh(mergeSet[0], mergeSet[1]);
    }

    internal::StartTheWorld();
  }

  const size_t _maxObjectSize;
  size_t _nextMeshCheck{0};

  // The big heap is handles malloc requests for large objects.  We
  // define a separate class to handle these to segregate bookkeeping
  // for large malloc requests from the ones used to back spans (which
  // are allocated from the arena)
  BigHeap _bigheap{};

  MiniHeap *_current[NumBins];

  mt19937_64 _prng;

  internal::vector<MiniHeap *> _littleheaps[NumBins]{};
  internal::map<uintptr_t, MiniHeap *> _miniheaps{};
};
}

#endif  // MESH__GLOBALMESHINGHEAP_H
