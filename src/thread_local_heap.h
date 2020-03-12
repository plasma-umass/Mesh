// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifndef MESH_THREAD_LOCAL_HEAP_H
#define MESH_THREAD_LOCAL_HEAP_H

#if !defined(_WIN32)
#include <pthread.h>
#include <stdalign.h>
#endif

#include <sys/types.h>

#include <algorithm>
#include <atomic>

#include "internal.h"
#include "mini_heap.h"
#include "shuffle_vector.h"

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

  ThreadLocalHeap(GlobalHeap *global, pthread_t pthreadCurrent)
      : _current(gettid()),
        _global(global),
        _pthreadCurrent(pthreadCurrent),
        _prng(internal::seed(), internal::seed()),
        _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)) {
    const auto arenaBegin = _global->arenaBegin();
    // when asked, give 16-byte allocations for 0-byte requests
    _shuffleVector[0].initialInit(arenaBegin, SizeMap::ByteSizeForClass(1));
    for (size_t i = 1; i < kNumBins; i++) {
      _shuffleVector[i].initialInit(arenaBegin, SizeMap::ByteSizeForClass(i));
    }
    d_assert(_global != nullptr);
  }

  ~ThreadLocalHeap() {
    releaseAll();
  }

  // pthread_set_sepcific destructor
  static void DestroyThreadLocalHeap(void *ptr);

  static void InitTLH();

  void releaseAll();

  void *ATTRIBUTE_NEVER_INLINE CACHELINE_ALIGNED_FN smallAllocSlowpath(size_t sizeClass);
  void *ATTRIBUTE_NEVER_INLINE CACHELINE_ALIGNED_FN smallAllocGlobalRefill(ShuffleVector &shuffleVector,
                                                                           size_t sizeClass);

  inline void *memalign(size_t alignment, size_t size) {
    // Check for non power-of-two alignment.
    if ((alignment == 0) || (alignment & (alignment - 1))) {
      return nullptr;
    }

    if (size < 8) {
      size = 8;
    }

    uint32_t sizeClass = 0;
    const bool isSmall = SizeMap::GetSizeClass(size, &sizeClass);
    if (alignment <= sizeof(double)) {
      // all of our size classes are at least 8-byte aligned
      auto ptr = this->malloc(size);
      d_assert_msg((reinterpret_cast<uintptr_t>(ptr) % alignment) == 0, "%p(%zu) %% %zu != 0", ptr, size, alignment);
      return ptr;
    } else if (isSmall) {
      const auto sizeClassBytes = SizeMap::ByteSizeForClass(sizeClass);
      // if the alignment is for a small allocation that is less than
      // the page size, and the size class size in bytes is a multiple
      // of the alignment, just call malloc
      if (sizeClassBytes <= kPageSize && alignment <= sizeClassBytes && (sizeClassBytes % alignment) == 0) {
        auto ptr = this->malloc(size);
        d_assert_msg((reinterpret_cast<uintptr_t>(ptr) % alignment) == 0, "%p(%zu) %% %zu != 0", ptr, size, alignment);
        return ptr;
      }
    }

    // fall back to page-aligned allocation
    const size_t pageAlignment = (alignment + kPageSize - 1) / kPageSize;
    const size_t pageCount = PageCount(size);
    return _global->pageAlignedAlloc(pageAlignment, pageCount);
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE realloc(void *oldPtr, size_t newSize) {
    if (oldPtr == nullptr) {
      return this->malloc(newSize);
    }

    if (newSize == 0) {
      this->free(oldPtr);
      return this->malloc(newSize);
    }

    size_t oldSize = getSize(oldPtr);

    // the following is directly from tcmalloc, designed to avoid
    // 'resizing ping pongs'
    const size_t lowerBoundToGrow = oldSize + oldSize / 4ul;
    const size_t upperBoundToShrink = oldSize / 2ul;

    if (newSize > oldSize || newSize < upperBoundToShrink) {
      void *newPtr = nullptr;
      if (newSize > oldSize && newSize < lowerBoundToGrow) {
        newPtr = this->malloc(lowerBoundToGrow);
      }
      if (newPtr == nullptr) {
        newPtr = this->malloc(newSize);
      }
      if (unlikely(newPtr == nullptr)) {
        return nullptr;
      }
      const size_t copySize = (oldSize < newSize) ? oldSize : newSize;
      memcpy(newPtr, oldPtr, copySize);
      this->free(oldPtr);
      return newPtr;
    } else {
      // the current allocation is good enough
      return oldPtr;
    }
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE calloc(size_t count, size_t size) {
    if (unlikely(size && count > (size_t)-1 / size)) {
      errno = ENOMEM;
      return nullptr;
    }

    const size_t n = count * size;
    void *ptr = this->malloc(n);

    if (ptr != nullptr) {
      memset(ptr, 0, n);
    }

    return ptr;
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE cxxNew(size_t sz) {
    void *ptr = this->malloc(sz);
    if (unlikely(ptr == NULL && sz != 0)) {
      throw std::bad_alloc();
    }

    return ptr;
  }

  // semiansiheap ensures we never see size == 0
  inline void *ATTRIBUTE_ALWAYS_INLINE malloc(size_t sz) {
    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass))) {
      return _global->malloc(sz);
    }

    ShuffleVector &shuffleVector = _shuffleVector[sizeClass];
    if (unlikely(shuffleVector.isExhausted())) {
      return smallAllocSlowpath(sizeClass);
    }

    return shuffleVector.malloc();
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *ptr) {
    if (unlikely(ptr == nullptr))
      return;

    size_t startEpoch{0};
    auto mh = _global->miniheapForWithEpoch(ptr, startEpoch);
    if (likely(mh && mh->current() == _current && !mh->hasMeshed())) {
      ShuffleVector &shuffleVector = _shuffleVector[mh->sizeClass()];
      shuffleVector.free(mh, ptr);
      return;
    }

    _global->freeFor(mh, ptr, startEpoch);
  }

  inline void ATTRIBUTE_ALWAYS_INLINE sizedFree(void *ptr, size_t sz) {
    this->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    auto mh = _global->miniheapFor(ptr);
    if (likely(mh && mh->current() == _current)) {
      ShuffleVector &shuffleVector = _shuffleVector[mh->sizeClass()];
      return shuffleVector.getSize();
    }

    return _global->getSize(ptr);
  }

  static ThreadLocalHeap *NewHeap(pthread_t current);
  static ThreadLocalHeap *GetHeapIfPresent() {
#ifdef MESH_HAVE_TLS
    return _threadLocalHeap;
#else
    return _tlhInitialized ? reinterpret_cast<ThreadLocalHeap *>(pthread_getspecific(_heapKey)) : nullptr;
#endif
  }

  static void DeleteHeap(ThreadLocalHeap *heap);

  static ThreadLocalHeap *GetHeap() {
    auto heap = GetHeapIfPresent();
    if (unlikely(heap == nullptr)) {
      return CreateHeapIfNecessary();
    }
    return heap;
  }

  static ThreadLocalHeap *ATTRIBUTE_NEVER_INLINE CreateHeapIfNecessary();

protected:
  ShuffleVector _shuffleVector[kNumBins] CACHELINE_ALIGNED;
  // this cacheline is read-mostly (only changed when creating + destroying threads)
  const pid_t _current CACHELINE_ALIGNED{0};
  GlobalHeap *_global;
  ThreadLocalHeap *_next{};  // protected by global heap lock
  ThreadLocalHeap *_prev{};
  const pthread_t _pthreadCurrent;
  MWC _prng CACHELINE_ALIGNED;
  const size_t _maxObjectSize;
  LocalHeapStats _stats{};
  bool _inSetSpecific;

#ifdef MESH_HAVE_TLS
  static __thread ThreadLocalHeap *_threadLocalHeap CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
#endif

  static ThreadLocalHeap *_threadLocalHeaps;
  static bool _tlhInitialized;
  static pthread_key_t _heapKey;
};

}  // namespace mesh

#endif  // MESH_THREAD_LOCAL_HEAP_H
