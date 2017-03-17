// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__LOCALMESHINGHEAP_H
#define MESH__LOCALMESHINGHEAP_H

#include <stdalign.h>
#include <algorithm>

#include "heaplayers.h"

#include "internal.h"
#include "miniheap.h"

using namespace HL;

namespace mesh {

template <int NumBins,                           // number of size classes
          int (*getSizeClass)(const size_t),     // same as for global
          size_t (*getClassMaxSize)(const int),  // same as for global
          int MeshPeriod,                        // perform meshing on average once every MeshPeriod frees
          typename GlobalMeshingHeap>
class LocalMeshingHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(LocalMeshingHeap);

public:
  enum { Alignment = 16 };

  LocalMeshingHeap(GlobalMeshingHeap *global)
      : _maxObjectSize(getClassMaxSize(NumBins - 1)), _prng(internal::seed()), _global(global) {
    static_assert(getClassMaxSize(NumBins - 1) == 16384, "expected 16k max object size");
    for (auto i = 0; i < NumBins; i++) {
      _current[i] = nullptr;
    }
    d_assert(_global != nullptr);
  }

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    if (sz <= 8) {
      sz = 8;
    } else if (sz < alignof(max_align_t)) {
      sz = alignof(max_align_t);
    } else {
      // Enforce alignment requirements: round up allocation sizes if
      // needed.  NOTE: Alignment needs to be a power of two.
      static_assert((alignof(max_align_t) & (alignof(max_align_t) - 1)) == 0, "Alignment not a power of two.");

      // Enforce alignment.
      sz = (sz + alignof(max_align_t) - 1UL) & ~(alignof(max_align_t) - 1UL);
    }

    const int sizeClass = getSizeClass(sz);
    const size_t sizeMax = getClassMaxSize(sizeClass);

    // d_assert_msg(sz <= sizeMax, "sz(%zu) shouldn't be greater than %zu (class %d)", sz, sizeMax, sizeClass);

    if (unlikely(sizeMax > _maxObjectSize))
      return _global->malloc(sz);

    // d_assert(sizeMax <= _maxObjectSize);
    // d_assert(sizeClass >= 0);
    // d_assert(sizeClass < NumBins);

    if (unlikely(_current[sizeClass] == nullptr)) {
      MiniHeap *mh = _global->allocMiniheap(sizeMax);
      if (unlikely(mh == nullptr))
        abort();

      _current[sizeClass] = mh;

      d_assert(_current[sizeClass] == mh);
    }

    MiniHeap *mh = _current[sizeClass];

    void *ptr = mh->malloc(_prng, sizeMax);
    if (unlikely(mh->isFull())) {
      mh->setDone();
      _current[sizeClass] = nullptr;
    }

    return ptr;
  }

  inline void free(void *ptr) {
    if (ptr == nullptr)
      return;

    for (size_t i = 0; i < NumBins; i++) {
      const auto curr = _current[i];
      if (curr && curr->contains(ptr)) {
        curr->free(ptr);
        return;
      }
    }

    _global->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (ptr == nullptr)
      return 0;

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
  GlobalMeshingHeap *_global;
};
}

#endif  // MESH__LOCALMESHINGHEAP_H
