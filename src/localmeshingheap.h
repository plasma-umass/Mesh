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

// forward declaration of StopTheWorld so we can declare it a friend
class StopTheWorld;

class STWThreadState {
private:
  DISALLOW_COPY_AND_ASSIGN(STWThreadState);

public:
  explicit STWThreadState();

  void insert(STWThreadState *head) {
    d_assert(head != nullptr);

    auto next = head->_next;
    head->_next = this;
    _next = next;
    _prev = head;
    next->_prev = this;
  }

  void unregister(int64_t currEpoch) {
    _shutdownEpoch.exchange(currEpoch);

    // TODO: do this asynchronously in a subsequent epoch.  We can't
    // free this here, because the thread cache structure may be used
    // when destroying thread-local storage (which happens after this
    // function is called)

    // auto prev = tc->_prev;
    // auto next = tc->_next;
    // prev->_next = next;
    // next->_prev = prev;
    // tc->_next = tc;
    // tc->_prev = tc;
  }

private:
  friend StopTheWorld;

  atomic_bool _waiting{false};
  atomic_int64_t _shutdownEpoch{-1};
  pthread_t _tid;
  STWThreadState *_prev{nullptr};
  STWThreadState *_next{nullptr};
};

class LocalHeapStats {
public:
  atomic_size_t allocCount;
  atomic_size_t freeCount;
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

      d_assert(_current[sizeClass] == mh);
    }

    MiniHeap *mh = _current[sizeClass];

    void *ptr = mh->malloc(sizeMax);
    if (unlikely(mh->isFull())) {
      mh->setDone();
      _current[sizeClass] = nullptr;
    }

    return ptr;
  }

  inline void free(void *ptr) {
    for (size_t i = 0; i < NumBins; i++) {
      const auto curr = _current[i];
      if (curr && curr->contains(ptr)) {
        curr->localFree(ptr, _prng, _mwc);
        return;
      }
    }

    _global->free(ptr);
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

  STWThreadState *stwState() {
    return &_stwState;
  }

protected:
  const size_t _maxObjectSize;
  MiniHeap *_current[NumBins];
  mt19937_64 _prng;
  MWC _mwc;
  GlobalHeap *_global;
  STWThreadState _stwState{};
  LocalHeapStats _stats{};
};
}  // namespace mesh

#endif  // MESH__LOCALMESHINGHEAP_H
