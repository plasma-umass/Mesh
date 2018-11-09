// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#ifndef MESH__THREAD_LOCAL_HEAP_H
#define MESH__THREAD_LOCAL_HEAP_H

#include <pthread.h>
#include <stdalign.h>

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

  ThreadLocalHeap(GlobalHeap *global)
      : _prng(internal::seed(), internal::seed()),
        _global(global),
        _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)) {
    // when asked, give 16-byte allocations for 0-byte requests
    _freelist[0].setObjectSize(SizeMap::ByteSizeForClass(1));
    for (size_t i = 1; i < kNumBins; i++) {
      _freelist[i].setObjectSize(SizeMap::ByteSizeForClass(i));
    }
    d_assert(_global != nullptr);
  }

  ~ThreadLocalHeap() {
    releaseAll();
  }

  void releaseAll() {
    for (size_t i = 1; i < kNumBins; i++) {
      auto mh = _freelist[i].detach();
      _global->releaseMiniheap(mh);
    }
  }

  void *smallAllocSlowpath(size_t sizeClass);

  inline void *memalign(size_t alignment, size_t size) {
    // Check for non power-of-two alignment.
    if ((alignment == 0) || (alignment & (alignment - 1))) {
      return nullptr;
    }

    uint32_t sizeClass = 0;
    const bool isSmall = SizeMap::GetSizeClass(size, &sizeClass);
    if (alignment == sizeof(double) || (isSmall && SizeMap::ByteSizeForClass(sizeClass) <= kPageSize &&
                                        alignment <= SizeMap::ByteSizeForClass(sizeClass))) {
      // the requested alignment will be naturally satisfied by our
      // malloc implementation.
      auto ptr = this->malloc(size);
      // but double-check that...
      d_assert_msg((reinterpret_cast<uintptr_t>(ptr) % alignment) == 0, "%p(%su) %% %zu != 0", ptr, size, alignment);
      return ptr;
    } else {
      const size_t pageAlignment = (alignment + kPageSize - 1) / kPageSize;
      const size_t pageCount = PageCount(size);
      return _global->pageAlignedAlloc(pageAlignment, pageCount);
    }
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

    Freelist &freelist = _freelist[sizeClass];
    if (unlikely(freelist.isExhausted())) {
      return smallAllocSlowpath(sizeClass);
    }

    _last = &freelist;
    return freelist.malloc();
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *ptr) {
    if (unlikely(ptr == nullptr))
      return;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      _last->free(ptr);
      return;
    }

    auto mh = _global->miniheapForLocked(ptr);
    if (likely(mh) && mh->maxCount() > 1) {
      Freelist &freelist = _freelist[mh->sizeClass()];
      // Freelists only refer to the first virtual span of a Miniheap.
      // Re-check contains() here to catch the case of a free for a
      // non-primary-span allocation.
      if (likely(freelist.getAttached() == mh && freelist.contains(ptr))) {
        d_assert(mh->isAttached());
        _last = &freelist;
        freelist.free(ptr);
        return;
      }
    }
    _global->free(ptr);
  }

  inline void ATTRIBUTE_ALWAYS_INLINE sizedFree(void *ptr, size_t sz) {
    if (unlikely(ptr == nullptr))
      return;

    uint32_t sizeClass = 0;

    // if the size isn't in our sizemap it is a large alloc
    if (unlikely(!SizeMap::GetSizeClass(sz, &sizeClass))) {
      _global->free(ptr);
      return;
    }

    Freelist &freelist = _freelist[sizeClass];
    if (likely(freelist.contains(ptr))) {
      freelist.free(ptr);
      return;
    }

    _global->free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    if (likely(_last != nullptr && _last->contains(ptr))) {
      return _last->getSize();
    }

    auto mh = _global->miniheapForLocked(ptr);
    if (likely(mh) && mh->maxCount() > 1) {
      Freelist &freelist = _freelist[mh->sizeClass()];
      if (likely(freelist.getAttached() == mh)) {
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
