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
          int MeshPeriod,                        // perform meshing on average once every MeshPeriod frees
          typename GlobalHeap>
class LocalMeshingHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(LocalMeshingHeap);

public:
  enum { Alignment = 16 };

  LocalMeshingHeap(GlobalHeap *global)
    : _maxObjectSize(SizeMap::ByteSizeForClass(NumBins - 1)),
        _prng(internal::seed()),
        _mwc(internal::seed(), internal::seed()),
        _global(global) {
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }
    d_assert(_global != nullptr);
  }

  // semiansiheap ensures we never see size == 0
  inline void *malloc(size_t sz) {
    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass)))
      return _global->malloc(sz);

    MiniHeap *mh = _current[sizeClass];
    if (unlikely(mh == nullptr)) {
      const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

      mh = _global->allocMiniheap(sizeMax);
      d_assert(mh->isAttached());
      if (unlikely(mh == nullptr))
        abort();

      _current[sizeClass] = mh;

      d_assert(_current[sizeClass] == mh);
    }

    // if (unlikely(!mh->isAttached() || mh->isExhausted())) {
    //   int attached = mh->isAttached();
    //   int exhausted = mh->isExhausted();
    //   mesh::debug("LocalHeap(sz: %zu): expecting failure %d %d (%zu/%zu)", sz, attached, exhausted, sizeMax,
    //   mh->objectSize());
    //   // mh->dumpDebug();
    //   // abort();
    // }

    bool isExhausted = false;
    void *ptr = mh->malloc(isExhausted);
    if (unlikely(isExhausted)) {
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
