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

template <size_t PageSize>
class ThreadLocalHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(ThreadLocalHeap);

public:
  enum { Alignment = 16 };
  using GlobalHeapT = GlobalHeap<PageSize>;
  using ShuffleVectorT = ShuffleVector<PageSize>;
  using MiniHeapT = MiniHeap<PageSize>;

  ThreadLocalHeap(GlobalHeapT *global, pthread_t pthreadCurrent)
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
  void *ATTRIBUTE_NEVER_INLINE CACHELINE_ALIGNED_FN smallAllocGlobalRefill(ShuffleVectorT &shuffleVector,
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
      if (sizeClassBytes <= PageSize && alignment <= sizeClassBytes && (sizeClassBytes % alignment) == 0) {
        auto ptr = this->malloc(size);
        d_assert_msg((reinterpret_cast<uintptr_t>(ptr) % alignment) == 0, "%p(%zu) %% %zu != 0", ptr, size, alignment);
        return ptr;
      }
    }

    // fall back to page-aligned allocation
    const size_t pageAlignment = (alignment + PageSize - 1) / PageSize;
    const size_t pageCount = PageCount(size);
    return _global->pageAlignedAlloc(pageAlignment, pageCount);
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE ATTRIBUTE_ALLOC_SIZE(3) realloc(void *oldPtr, size_t newSize) {
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

  inline void *ATTRIBUTE_ALWAYS_INLINE ATTRIBUTE_MALLOC ATTRIBUTE_ALLOC_SIZE2(2, 3) calloc(size_t count, size_t size) {
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
  inline void *ATTRIBUTE_ALWAYS_INLINE ATTRIBUTE_MALLOC malloc(size_t sz) {
    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass))) {
      return _global->malloc(sz);
    }

    ShuffleVectorT &shuffleVector = _shuffleVector[sizeClass];
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
      ShuffleVectorT &shuffleVector = _shuffleVector[mh->sizeClass()];
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
      ShuffleVectorT &shuffleVector = _shuffleVector[mh->sizeClass()];
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
  ShuffleVectorT _shuffleVector[kNumBins] CACHELINE_ALIGNED;
  // this cacheline is read-mostly (only changed when creating + destroying threads)
  const pid_t _current CACHELINE_ALIGNED{0};
  GlobalHeapT *_global;
  ThreadLocalHeap *_next{};  // protected by global heap lock
  ThreadLocalHeap *_prev{};
  const pthread_t _pthreadCurrent;
  MWC _prng CACHELINE_ALIGNED;
  const size_t _maxObjectSize;
  LocalHeapStats _stats{};
  bool _inSetSpecific{false};

#ifdef MESH_HAVE_TLS
  static __thread ThreadLocalHeap *_threadLocalHeap CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
#endif

  static ThreadLocalHeap *_threadLocalHeaps;
  static bool _tlhInitialized;
  static pthread_key_t _heapKey;
};

#ifdef MESH_HAVE_TLS
template <size_t PageSize>
__thread ThreadLocalHeap<PageSize> *ThreadLocalHeap<PageSize>::_threadLocalHeap CACHELINE_ALIGNED;
#endif
template <size_t PageSize>
ThreadLocalHeap<PageSize> *ThreadLocalHeap<PageSize>::_threadLocalHeaps{nullptr};
template <size_t PageSize>
bool ThreadLocalHeap<PageSize>::_tlhInitialized{false};
template <size_t PageSize>
pthread_key_t ThreadLocalHeap<PageSize>::_heapKey{0};

template <size_t PageSize>
void ThreadLocalHeap<PageSize>::DestroyThreadLocalHeap(void *ptr) {
  if (ptr != nullptr) {
#ifdef MESH_HAVE_TLS
    _threadLocalHeap = nullptr;
#endif
    DeleteHeap(reinterpret_cast<ThreadLocalHeap *>(ptr));
  }
}

template <size_t PageSize>
void ThreadLocalHeap<PageSize>::InitTLH() {
  hard_assert(!_tlhInitialized);
  pthread_key_create(&_heapKey, DestroyThreadLocalHeap);
  _tlhInitialized = true;
}

template <size_t PageSize>
ThreadLocalHeap<PageSize> *ThreadLocalHeap<PageSize>::NewHeap(pthread_t current) {
  // we just allocate out of our internal heap
  void *buf = mesh::internal::Heap().malloc(sizeof(ThreadLocalHeap));
  // Increased to 128KB to accommodate larger shuffle vectors with 1024 uint16_t entries
  // Each sv::Entry is now 4 bytes (2x uint16_t), doubling the _list array size
  static_assert(sizeof(ThreadLocalHeap) < 4096 * 32, "tlh should have a reasonable size");
  hard_assert(buf != nullptr);
  hard_assert(reinterpret_cast<uintptr_t>(buf) % CACHELINE_SIZE == 0);

  auto heap = new (buf) ThreadLocalHeap(&mesh::runtime<PageSize>().heap(), current);

  heap->_prev = nullptr;
  heap->_next = _threadLocalHeaps;
  if (_threadLocalHeaps != nullptr) {
    _threadLocalHeaps->_prev = heap;
  }
  _threadLocalHeaps = heap;

  return heap;
}

template <size_t PageSize>
ThreadLocalHeap<PageSize> *ThreadLocalHeap<PageSize>::CreateHeapIfNecessary() {
#ifdef MESH_HAVE_TLS
  const bool maybeReentrant = !_tlhInitialized;
  // check to see if we really need to create the heap
  if (_tlhInitialized && _threadLocalHeap != nullptr) {
    return _threadLocalHeap;
  }
#else
  const bool maybeReentrant = true;
#endif

  ThreadLocalHeap *heap = nullptr;

  {
    std::lock_guard<GlobalHeapT> lock(mesh::runtime<PageSize>().heap());

    const pthread_t current = pthread_self();

    if (maybeReentrant) {
      for (ThreadLocalHeap *h = _threadLocalHeaps; h != nullptr; h = h->_next) {
        if (h->_pthreadCurrent == current) {
          heap = h;
          break;
        }
      }
    }

    if (heap == nullptr) {
      heap = NewHeap(current);
    }
  }

  if (!heap->_inSetSpecific && _tlhInitialized) {
    heap->_inSetSpecific = true;
#ifdef MESH_HAVE_TLS
    _threadLocalHeap = heap;
#endif
    pthread_setspecific(_heapKey, heap);
    heap->_inSetSpecific = false;
  }

  return heap;
}

template <size_t PageSize>
void ThreadLocalHeap<PageSize>::DeleteHeap(ThreadLocalHeap *heap) {
  if (heap == nullptr) {
    return;
  }

  {
    // Hold the global heap lock while manipulating the linked list.
    // This prevents races with NewHeap and other concurrent DeleteHeap calls.
    std::lock_guard<GlobalHeapT> lock(mesh::runtime<PageSize>().heap());

    ThreadLocalHeap *next = heap->_next;
    ThreadLocalHeap *prev = heap->_prev;

    if (next != nullptr) {
      next->_prev = prev;
    }
    if (prev != nullptr) {
      prev->_next = next;
    }
    if (_threadLocalHeaps == heap) {
      _threadLocalHeaps = next;
    }
  }

  // Call destructor and free outside the lock to avoid deadlock.
  // The destructor calls releaseAll() which acquires miniheap locks,
  // and the global heap lock() already holds all miniheap locks.
  heap->ThreadLocalHeap::~ThreadLocalHeap();
  mesh::internal::Heap().free(reinterpret_cast<void *>(heap));
}

template <size_t PageSize>
void ThreadLocalHeap<PageSize>::releaseAll() {
  for (size_t i = 1; i < kNumBins; i++) {
    _shuffleVector[i].refillMiniheaps();
    _global->releaseMiniheaps(_shuffleVector[i].miniheaps());
  }
}

// we get here if the shuffleVector is exhausted
template <size_t PageSize>
void *CACHELINE_ALIGNED_FN ThreadLocalHeap<PageSize>::smallAllocSlowpath(size_t sizeClass) {
  ShuffleVectorT &shuffleVector = _shuffleVector[sizeClass];

  // we grab multiple MiniHeaps at a time from the global heap.  often
  // it is possible to refill the freelist from a not-yet-used
  // MiniHeap we already have, without global cross-thread
  // synchronization
  if (likely(shuffleVector.localRefill())) {
    return shuffleVector.malloc();
  }

  return smallAllocGlobalRefill(shuffleVector, sizeClass);
}

template <size_t PageSize>
void *CACHELINE_ALIGNED_FN ThreadLocalHeap<PageSize>::smallAllocGlobalRefill(ShuffleVectorT &shuffleVector,
                                                                             size_t sizeClass) {
  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  _global->allocSmallMiniheaps(sizeClass, sizeMax, shuffleVector.miniheaps(), _current);
  shuffleVector.reinit();

  d_assert(!shuffleVector.isExhausted());

  void *ptr = shuffleVector.malloc();
  d_assert(ptr != nullptr);

  return ptr;
}

}  // namespace mesh

#endif  // MESH_THREAD_LOCAL_HEAP_H