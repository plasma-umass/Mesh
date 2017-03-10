// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__LOCALMESHINGHEAP_H
#define MESH__LOCALMESHINGHEAP_H

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

      // d_assert(_current[sizeClass] == mh);
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
    // FIXME: check _current

    _global->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    // FIXME: check _current

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
