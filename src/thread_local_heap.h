// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__THREAD_LOCAL_HEAP_H
#define MESH__THREAD_LOCAL_HEAP_H

#include <pthread.h>
#include <stdalign.h>

#include <algorithm>
#include <atomic>

#include "internal.h"
#include "miniheap.h"

#include "rng/mwc.h"

#include "heaplayers.h"

#include "runtime.h"

using namespace HL;

namespace mesh {

class LocalHeapStats {
public:
  atomic_size_t allocCount{0};
  atomic_size_t freeCount{0};
};

class ThreadLocalHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(ThreadLocalHeap);

public:
  enum { Alignment = 16 };

  ThreadLocalHeap(GlobalHeap *global)
      : _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)),
        _prng(internal::seed()),
        _mwc(internal::seed(), internal::seed()),
        _global(global) {
    for (auto i = 0; i < kNumBins; i++) {
      _current[i] = nullptr;
    }
    d_assert(_global != nullptr);
  }

  // semiansiheap ensures we never see size == 0
  inline void *ATTRIBUTE_ALWAYS_INLINE malloc(size_t sz) {
    // Prevent integer underflows. This maximum should (and
    // currently does) provide more than enough slack to compensate for any
    // rounding below (in the alignment section).
    if (unlikely(sz > INT_MAX || sz == 0)) {
      return 0;
    }

    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass))) {
      return _global->malloc(sz);
    }

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
      if (mh == _last) {
        _last = nullptr;
      }
      mh->detach();
      _current[sizeClass] = nullptr;
    } else {
      _last = mh;
    }

    return ptr;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *ptr) {
    if (unlikely(ptr == nullptr))
        return;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      _last->localFree(ptr, _prng, _mwc);
      return;
    }

    for (size_t i = 0; i < kNumBins; i++) {
      const auto curr = _current[i];
      if (curr && curr->contains(ptr)) {
        curr->localFree(ptr, _prng, _mwc);
        _last = curr;
        return;
      }
    }

    _global->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      return _last->getSize(ptr);
    }

    for (size_t i = 0; i < kNumBins; i++) {
      const auto curr = _current[i];
      if (curr && curr->contains(ptr)) {
        _last = curr;
        return curr->getSize(ptr);
      }
    }

    return _global->getSize(ptr);
  }

  static inline ThreadLocalHeap *GetFastPathHeap() {
    return _threadLocalData.fastpathHeap;
  }

  static ATTRIBUTE_NEVER_INLINE ThreadLocalHeap *GetHeap();

  static inline ThreadLocalHeap *CreateThreadLocalHeap() {
    void *buf = mesh::internal::Heap().malloc(sizeof(ThreadLocalHeap));
    if (buf == nullptr) {
      mesh::debug("mesh: unable to allocate ThreadLocalHeap, aborting.\n");
      abort();
    }

    return new (buf) ThreadLocalHeap(&mesh::runtime().heap());
  }

protected:
  const size_t _maxObjectSize;
  MiniHeap *_last{nullptr};
  MiniHeap *_current[kNumBins];
  mt19937_64 _prng;
  MWC _mwc;
  GlobalHeap *_global;
  LocalHeapStats _stats{};

  struct ThreadLocalData {
    ThreadLocalHeap *fastpathHeap;
  };
  static __thread ThreadLocalData _threadLocalData CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
};
}  // namespace mesh

#endif  // MESH__THREAD_LOCAL_HEAP_H
