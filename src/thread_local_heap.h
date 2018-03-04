// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__THREAD_LOCAL_HEAP_H
#define MESH__THREAD_LOCAL_HEAP_H

#include <pthread.h>
#include <stdalign.h>

#include <algorithm>
#include <atomic>

#include "freelist.h"
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
      : _prng(internal::seed(), internal::seed()),
        _global(global),
        _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)) {
    for (size_t i = 0; i < kNumBins; i++) {
      auto objectSize = SizeMap::ByteSizeForClass(i);
      // treat 0-sized requests as just a duplication of the 16-byte
      if (i == 0)
        objectSize = SizeMap::ByteSizeForClass(1);
      _freelist[i].setObjectSize(objectSize);
    }
    d_assert(_global != nullptr);
  }

  void attachFreelist(Freelist &freelist, size_t sizeClass);

  void *mallocSlowpath(size_t sz);

  // semiansiheap ensures we never see size == 0
  inline void *ATTRIBUTE_ALWAYS_INLINE malloc(size_t sz) {
    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass))) {
      return _global->malloc(sz);
    }

    Freelist &freelist = _freelist[sizeClass];
    if (unlikely(freelist.isExhausted())) {
      return mallocSlowpath(sz);
    }

    void *ptr = freelist.malloc();
    _last = &freelist;

    return ptr;
  }

  void freeSlowpath(void *ptr);

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *ptr) {
    if (unlikely(ptr == nullptr))
      return;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      _last->free(ptr);
    } else {
      freeSlowpath(ptr);
    }
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

  static ThreadLocalHeap *CreateThreadLocalHeap();

protected:
  Freelist _freelist[kNumBins] CACHELINE_ALIGNED;
  Freelist *_last{nullptr};
  MWC _prng;
  GlobalHeap *_global;
  const size_t _maxObjectSize;
  LocalHeapStats _stats{};

  struct ThreadLocalData {
    ThreadLocalHeap *fastpathHeap;
  };
  static __thread ThreadLocalData _threadLocalData CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
};
}  // namespace mesh

#endif  // MESH__THREAD_LOCAL_HEAP_H
