// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__GLOBAL_HEAP_H
#define MESH__GLOBAL_HEAP_H

#include <algorithm>
#include <mutex>

#include "binnedtracker.h"
#include "internal.h"
#include "meshable-arena.h"
#include "meshing.h"
#include "miniheap.h"

#include "heaplayers.h"

using namespace HL;

namespace mesh {

class GlobalHeapStats {
public:
  atomic_size_t meshCount;
  size_t mhFreeCount;
  size_t mhAllocCount;
  size_t mhHighWaterMark;
};

class GlobalHeap : public MeshableArena {
private:
  DISALLOW_COPY_AND_ASSIGN(GlobalHeap);
  typedef MeshableArena Super;

  static_assert(HL::gcd<MmapHeap::Alignment, Alignment>::value == Alignment,
                "expected MmapHeap to have 16-byte alignment");

  struct MeshArguments {
    GlobalHeap *instance;
    internal::vector<std::pair<MiniHeap *, MiniHeap *>> mergeSets;
  };

public:
  enum { Alignment = 16 };

  GlobalHeap()
      : _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)),
        _prng(internal::seed()),
        _fastPrng(internal::seed(), internal::seed()),
        _lastMesh{std::chrono::high_resolution_clock::now()} {
  }

  inline void dumpStrings() const {
    lock_guard<mutex> lock(_miniheapLock);

    for (size_t i = 0; i < kNumBins; i++) {
      _littleheaps[i].printOccupancy();
    }
  }

  void dumpStats(int level, bool beDetailed) const;

  // must be called with exclusive _mhRWLock held
  inline MiniHeap *ATTRIBUTE_ALWAYS_INLINE allocMiniheapLocked(int sizeClass, size_t pageCount, size_t objectCount,
                                                               size_t objectSize, size_t pageAlignment = 1) {
    void *buf = _mhAllocator.alloc();
    hard_assert(buf != nullptr);

    // allocate out of the arena
    void *span = Super::pageAlloc(pageCount, buf, pageAlignment);
    hard_assert(span != nullptr);
    d_assert((reinterpret_cast<uintptr_t>(span) / kPageSize) % pageAlignment == 0);

    const size_t spanSize = kPageSize * pageCount;
    d_assert(0 < spanSize);

    MiniHeap *mh = new (buf) MiniHeap(span, objectCount, objectSize, spanSize);

    if (sizeClass >= 0)
      trackMiniheapLocked(sizeClass, mh);

    _miniheapCount++;
    _stats.mhAllocCount++;
    _stats.mhHighWaterMark = max(_miniheapCount, _stats.mhHighWaterMark);
    //_stats.mhClassHWM[sizeClass] = max(_littleheapCounts[sizeClass], _stats.mhClassHWM[sizeClass].load());

    return mh;
  }

  inline void *pageAlignedAlloc(size_t pageAlignment, size_t pageCount) {
    lock_guard<mutex> lock(_miniheapLock);

    MiniHeap *mh = allocMiniheapLocked(-1, pageCount, 1, pageCount * kPageSize, pageAlignment);

    d_assert(mh->maxCount() == 1);
    d_assert(mh->spanSize() == pageCount * kPageSize);
    d_assert(mh->objectSize() == pageCount * kPageSize);

    void *ptr = mh->mallocAt(0);

    mh->unref();

    return ptr;
  }

  inline MiniHeap *allocSmallMiniheap(int sizeClass, size_t objectSize) {
    lock_guard<mutex> lock(_miniheapLock);

    d_assert(objectSize <= _maxObjectSize);

#ifndef NDEBUG
    const size_t classMaxSize = SizeMap::ByteSizeForClass(sizeClass);

    d_assert_msg(objectSize == classMaxSize, "sz(%zu) shouldn't be greater than %zu (class %d)", objectSize,
                 classMaxSize, sizeClass);
    d_assert(sizeClass >= 0);
#endif
    d_assert(sizeClass < kNumBins);

    // check our bins for a miniheap to reuse
    MiniHeap *existing = _littleheaps[sizeClass].selectForReuse();
    if (existing != nullptr) {
      existing->ref();
      d_assert(existing->refcount() == 1);
      return existing;
    }

    // if we have objects bigger than the size of a page, allocate
    // multiple pages to amortize the cost of creating a
    // miniheap/globally locking the heap.  For example, asking for
    // 2048 byte objects would allocate 4 4KB pages.
    const size_t objectCount = max(kPageSize / objectSize, kMinStringLen);
    const size_t pageCount = PageCount(objectSize * objectCount);

    auto mh = allocMiniheapLocked(sizeClass, pageCount, objectCount, objectSize);
    return mh;
  }

  // large, page-multiple allocations
  void *malloc(size_t sz);

  inline MiniHeap *miniheapForLocked(const void *ptr) const {
    auto mh = reinterpret_cast<MiniHeap *>(Super::lookupMiniheap(ptr));
    __builtin_prefetch(mh, 1, 2);
    return mh;
  }

  // if the MiniHeap is non-null, its reference count is increased by one
  inline MiniHeap *miniheapFor(const void *ptr) const {
    lock_guard<mutex> lock(_miniheapLock);

    auto mh = miniheapForLocked(ptr);
    if (likely(mh != nullptr)) {
      mh->ref();
    }

    return mh;
  }

  void trackMiniheapLocked(size_t sizeClass, MiniHeap *mh) {
    _littleheaps[sizeClass].add(mh);
  }

  void untrackMiniheapLocked(size_t sizeClass, MiniHeap *mh) {
    _stats.mhAllocCount -= 1;
    _littleheaps[sizeClass].remove(mh);
  }

  // called with lock held
  void freeMiniheapAfterMeshLocked(MiniHeap *mh, bool untrack = true) {
    if (untrack) {
      const auto sizeClass = SizeMap::SizeClass(mh->objectSize());
      untrackMiniheapLocked(sizeClass, mh);
    }

    mh->MiniHeap::~MiniHeap();
    // memset(reinterpret_cast<char *>(mh), 0x77, 128);
    _mhAllocator.free(mh);
    _miniheapCount--;
  }

  void freeMiniheap(MiniHeap *&mh, bool untrack = true) {
    lock_guard<mutex> lock(_miniheapLock);
    freeMiniheapLocked(mh, untrack);
  }

  void freeMiniheapLocked(MiniHeap *&mh, bool untrack) {
    const auto spans = mh->spans();
    const auto spanSize = mh->spanSize();

    const auto meshCount = mh->meshCount();
    for (size_t i = 0; i < meshCount; i++) {
      // if (i > 0)
      //   debug("Super::freeing meshed span\n");
      const auto type = i == 0 ? internal::PageType::Dirty : internal::PageType::Meshed;
      Super::free(reinterpret_cast<void *>(spans[i]), spanSize, type);
    }

    _stats.mhFreeCount++;

    freeMiniheapAfterMeshLocked(mh, untrack);
    mh = nullptr;
  }

  inline void flushBinLocked(size_t sizeClass) {
    auto emptyMiniheaps = _littleheaps[sizeClass].getFreeMiniheaps();
    for (size_t i = 0; i < emptyMiniheaps.size(); i++) {
      freeMiniheapLocked(emptyMiniheaps[i], false);
    }
  }

  void free(void *ptr);

  inline size_t getSize(void *ptr) const {
    if (unlikely(ptr == nullptr))
      return 0;

    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      auto size = mh->getSize(ptr);
      mh->unref();
      return size;
    } else {
      return 0;
    }
  }

  inline bool inBounds(void *ptr) const {
    debug("TODO: I don't think this is correct.");

    if (unlikely(ptr == nullptr))
      return false;

    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      mh->unref();
      return true;
    }
    return false;
  }

  int bitmapGet(enum mesh::BitType type, void *ptr) const {
    if (unlikely(ptr == nullptr))
      return 0;

    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      auto result = mh->bitmapGet(type, ptr);
      mh->unref();
      return result;
    } else {
      internal::__mesh_assert_fail("TODO: bitmap on bigheap", __FILE__, __PRETTY_FUNCTION__, __LINE__, "");
    }
  }

  int bitmapSet(enum mesh::BitType type, void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      auto result = mh->bitmapSet(type, ptr);
      mh->unref();
      return result;
    } else {
      internal::__mesh_assert_fail("TODO: bitmap on bigheap", __FILE__, __PRETTY_FUNCTION__, __LINE__, "");
    }
  }

  int bitmapClear(enum mesh::BitType type, void *ptr) {
    if (unlikely(ptr == nullptr))
      return 0;

    auto mh = miniheapFor(ptr);
    if (likely(mh)) {
      auto result = mh->bitmapClear(type, ptr);
      mh->unref();
      return result;
    } else {
      internal::__mesh_assert_fail("TODO: bitmap on bigheap", __FILE__, __PRETTY_FUNCTION__, __LINE__, "");
    }
  }

  int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

  size_t getAllocatedMiniheapCount() const {
    lock_guard<mutex> lock(_miniheapLock);
    return _miniheapCount;
  }

  void setMeshPeriodSecs(double period) {
    _meshPeriodSecs = period;
  }

  void lock() {
    _miniheapLock.lock();
    // internal::Heap().lock();
  }

  void unlock() {
    // internal::Heap().unlock();
    _miniheapLock.unlock();
  }

  // PUBLIC ONLY FOR TESTING
  // after call to meshLocked() completes src is a nullptr
  void meshLocked(MiniHeap *dst, MiniHeap *&src) {
    if (dst->meshCount() + src->meshCount() > kMaxMeshes)
      return;

    const size_t dstSpanSize = dst->spanSize();
    const auto dstSpanStart = reinterpret_cast<void *>(dst->getSpanStart());

    const auto srcSpans = src->spans();
    const auto srcMeshCount = src->meshCount();

    for (size_t i = 0; i < srcMeshCount; i++) {
      // marks srcSpans read-only
      Super::beginMesh(dstSpanStart, srcSpans[i], dstSpanSize);
    }

    // does the copying of objects and updating of span metadata
    dst->consume(src);

    for (size_t i = 0; i < srcMeshCount; i++) {
      // frees physical memory + re-marks srcSpans as read/write
      Super::finalizeMesh(dstSpanStart, srcSpans[i], dstSpanSize);
    }

    // make sure we adjust what bin the destination is in -- it might
    // now be full and not a candidate for meshing
    d_assert(dst->refcount() == 0);
    _littleheaps[SizeMap::SizeClass(dst->objectSize())].postFree(dst, dst->inUseCount());
    freeMiniheapAfterMeshLocked(src);
    src = nullptr;
  }

  inline void maybeMesh() {
    if (_meshPeriod == 0)
      return;
    // if (_smallFreeCount == 0)
    //   return;

    const auto now = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double> duration = now - _lastMesh;

    if (unlikely(_meshPeriodSecs <= 0))
      return;
    if (likely(duration.count() < _meshPeriodSecs))
      return;

    _lastMesh = now;
    meshAllSizeClasses();
  }

  inline bool okToProceed(void *ptr) const {
    lock_guard<mutex> lock(_miniheapLock);

    if (ptr == nullptr)
      return false;

    return miniheapForLocked(ptr) != nullptr;
  }

protected:
  // check for meshes in all size classes -- must be called unlocked
  void meshAllSizeClasses();

  const size_t _maxObjectSize;
  atomic_size_t _lastMeshEffective{0};
  atomic_size_t _meshPeriod{kDefaultMeshPeriod};

  // always accessed with the mhRWLock exclusively locked
  size_t _miniheapCount{0};

  mt19937_64 _prng;
  MWC _fastPrng;

  BinnedTracker<MiniHeap> _littleheaps[kNumBins];

  mutable mutex _miniheapLock{};

  GlobalHeapStats _stats{};

  double _meshPeriodSecs{kMeshPeriodSecs};
  // XXX: should be atomic, but has exception spec?
  std::chrono::time_point<std::chrono::high_resolution_clock> _lastMesh;
};
}  // namespace mesh

#endif  // MESH__GLOBAL_HEAP_H
