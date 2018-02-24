// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__LOCALMESHINGHEAP_H
#define MESH__LOCALMESHINGHEAP_H

#include <pthread.h>
#include <stdalign.h>

#include <algorithm>
#include <atomic>

#include "internal.h"
#include "miniheap.h"

#include "rng/mwc.h"

#include "heaplayers.h"

using namespace HL;

namespace mesh {

class LocalHeapStats {
public:
  atomic_size_t allocCount{0};
  atomic_size_t freeCount{0};
};

template <int NumBins,                           // number of size classes
          int (*getSizeClass)(const size_t),     // same as for global
          size_t (*getClassMaxSize)(const int),  // same as for global
          int MeshPeriod,                        // perform meshing on average once every MeshPeriod frees
          typename GlobalHeap>
class LocalMeshingHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(LocalMeshingHeap);

public:
  enum { Alignment = 16 };

  LocalMeshingHeap(GlobalHeap *global)
      : _maxObjectSize(getClassMaxSize(NumBins - 1)),
        _prng(internal::seed()),
        _mwc(internal::seed(), internal::seed()),
        _global(global) {
    // static_assert(getClassMaxSize(NumBins - 1) == 16384, "expected 16k max object size");
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }
    d_assert(_global != nullptr);
  }

  // semiansiheap ensures we never see size == 0
  inline void *malloc(size_t sz) {
    uint32_t sizeClass = 0;
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass)))
      return _global->malloc(sz);

    // d_assert_msg(sz <= sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", sz, sizeMax, sizeClass);

    // d_assert(sizeMax <= _maxObjectSize);
    // d_assert(sizeClass >= 0);
    // d_assert(sizeClass < NumBins);

    if (unlikely(_current[sizeClass] == nullptr)) {
      const size_t sizeMax = getClassMaxSize(sizeClass);

      MiniHeap *mh = _global->allocMiniheap(sizeMax);
      d_assert(mh->isAttached());
      if (unlikely(mh == nullptr))
        abort();

      _current[sizeClass] = mh;

      d_assert(_current[sizeClass] == mh);
    }

    MiniHeap *mh = _current[sizeClass];
    // if (unlikely(!mh->isAttached() || mh->isExhausted())) {
    //   int attached = mh->isAttached();
    //   int exhausted = mh->isExhausted();
    //   mesh::debug("LocalHeap(sz: %zu): expecting failure %d %d (%zu/%zu)", sz, attached, exhausted, sizeMax,
    //   mh->objectSize());
    //   // mh->dumpDebug();
    //   // abort();
    // }

    void *ptr = mh->malloc(0);  // arg doesn't matter -- MiniHeap knows the size
    if (unlikely(mh->isExhausted())) {
      mh->detach();
      _current[sizeClass] = nullptr;
    }

    return ptr;
  }

  inline void free(void *ptr) {
    auto mh = _global->UNSAFEMiniheapFor(ptr);
    if (likely(mh != nullptr && mh->isOwnedBy(pthread_self()))) {
      mh->localFree(ptr, _prng, _mwc);
    } else {
      _global->free(ptr);
    }
  }

  inline size_t getSize(void *ptr) {
    for (size_t i = 0; i < NumBins; i++) {
      const auto curr = _current[i];
      if (curr && curr->contains(ptr)) {
        return curr->getSize(ptr);
      }
    }

    return _global->getSize(ptr);
  }

protected:
  const size_t _maxObjectSize;
  MiniHeap *_current[NumBins];
  mt19937_64 _prng;
  MWC _mwc;
  GlobalHeap *_global;
  LocalHeapStats _stats{};
};
}  // namespace mesh

#endif  // MESH__LOCALMESHINGHEAP_H
