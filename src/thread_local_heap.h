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
#include "freelist.h"

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
        _prng(internal::seed(), internal::seed()),
        _global(global) {
    // start at 1, becuase 0 is an unused '0' size class
    for (size_t i = 1; i < kNumBins; i++) {
      _freelist[i].setObjectSize(SizeMap::ByteSizeForClass(i));
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

    Freelist &freelist = _freelist[sizeClass];
    if (unlikely(!freelist.isAttached())) {
      const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

      MiniHeap *mh = _global->allocMiniheap(sizeMax);

      freelist.attach(_prng, mh);
      d_assert(!freelist.isExhausted());

      // tell the miniheap how many offsets we pulled out/preallocated into our freelist
      mh->reattach(freelist.length());

      d_assert(mh->isAttached());
#ifndef NDEBUG
      if (unlikely(mh == nullptr))
        abort();
#endif
    }

    bool isExhausted = false;
    void *ptr = freelist.malloc(isExhausted);
    if (unlikely(isExhausted)) {
      if (&freelist == _last) {
        _last = nullptr;
      }
      freelist.detach();
    } else {
      _last = &freelist;
    }

    return ptr;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *ptr) {
    if (unlikely(ptr == nullptr))
      return;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      _last->free(_prng, ptr);
      return;
    }

    for (size_t i = 0; i < kNumBins; i++) {
      Freelist &freelist = _freelist[i];
      if (freelist.contains(ptr)) {
        freelist.free(_prng, ptr);
        _last = &freelist;
        return;
      }
    }

    _global->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      return _last->getSize();
    }

    for (size_t i = 0; i < kNumBins; i++) {
      Freelist &freelist = _freelist[i];
      if (freelist.contains(ptr)) {
        _last = &freelist;
        return freelist.getSize();
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
  Freelist *_last{nullptr};
  MWC _prng;
  GlobalHeap *_global;
  LocalHeapStats _stats{};
  Freelist _freelist[kNumBins] CACHELINE_ALIGNED;

  struct ThreadLocalData {
    ThreadLocalHeap *fastpathHeap;
  };
  static __thread ThreadLocalData _threadLocalData CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
};
}  // namespace mesh

#endif  // MESH__THREAD_LOCAL_HEAP_H
