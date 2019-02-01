// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__GLOBAL_HEAP_H
#define MESH__GLOBAL_HEAP_H

#include <algorithm>
#include <mutex>

#include "binned_tracker.h"
#include "internal.h"
#include "meshable_arena.h"
#include "meshing.h"
#include "mini_heap.h"

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
        _fastPrng(internal::seed(), internal::seed()),
        _lastMesh{std::chrono::high_resolution_clock::now()} {
  }

  inline void dumpStrings() const {
    lock_guard<mutex> lock(_miniheapLock);

    for (size_t i = 0; i < kNumBins; i++) {
      _littleheaps[i].printOccupancy();
    }
  }

  inline void flushAllBins() {
    for (size_t sizeClass = 0; sizeClass < kNumBins; sizeClass++) {
      flushBinLocked(sizeClass);
    }
  }

  void scavenge(bool force = false) {
    lock_guard<mutex> lock(_miniheapLock);

    Super::scavenge(force);
  }

  void dumpStats(int level, bool beDetailed) const;

  // must be called with exclusive _mhRWLock held
  inline MiniHeap *ATTRIBUTE_ALWAYS_INLINE allocMiniheapLocked(int sizeClass, size_t pageCount, size_t objectCount,
                                                               size_t objectSize, size_t pageAlignment = 1) {
    d_assert(0 < pageCount);

    void *buf = _mhAllocator.alloc();
    d_assert(buf != nullptr);

    // allocate out of the arena
    Span span{0, 0};
    char *spanBegin = Super::pageAlloc(span, pageCount, pageAlignment);
    d_assert(spanBegin != nullptr);
    d_assert((reinterpret_cast<uintptr_t>(spanBegin) / kPageSize) % pageAlignment == 0);

    const auto miniheapID = MiniHeapID{_mhAllocator.offsetFor(buf)};
    Super::trackMiniHeap(span, miniheapID);

    MiniHeap *mh = new (buf) MiniHeap(arenaBegin(), span, objectCount, objectSize);

    if (sizeClass >= 0)
      trackMiniheapLocked(mh);

    _miniheapCount++;
    _stats.mhAllocCount++;
    _stats.mhHighWaterMark = max(_miniheapCount, _stats.mhHighWaterMark);

    return mh;
  }

  inline void *pageAlignedAlloc(size_t pageAlignment, size_t pageCount) {
    lock_guard<mutex> lock(_miniheapLock);

    MiniHeap *mh = allocMiniheapLocked(-1, pageCount, 1, pageCount * kPageSize, pageAlignment);

    d_assert(mh->maxCount() == 1);
    d_assert(mh->spanSize() == pageCount * kPageSize);
    // d_assert(mh->objectSize() == pageCount * kPageSize);

    void *ptr = mh->mallocAt(arenaBegin(), 0);

    return ptr;
  }

  inline void releaseMiniheapLocked(MiniHeap *mh, int sizeClass) {
    mh->unsetAttached();
    _littleheaps[sizeClass].postFree(mh, mh->inUseCount());
  }

  inline void releaseMiniheap(MiniHeap *mh) {
    if (mh == nullptr) {
      return;
    }

    lock_guard<mutex> lock(_miniheapLock);
    releaseMiniheapLocked(mh, mh->sizeClass());
  }

  inline MiniHeap *allocSmallMiniheap(int sizeClass, size_t objectSize, MiniHeap *oldMH, pid_t current) {
    lock_guard<mutex> lock(_miniheapLock);

    d_assert(sizeClass >= 0);

    // ensure this flag is always set with the miniheap lock held
    if (oldMH != nullptr) {
      releaseMiniheapLocked(oldMH, sizeClass);
    }

    d_assert(objectSize <= _maxObjectSize);

#ifndef NDEBUG
    const size_t classMaxSize = SizeMap::ByteSizeForClass(sizeClass);

    d_assert_msg(objectSize == classMaxSize, "sz(%zu) shouldn't be greater than %zu (class %d)", objectSize,
                 classMaxSize, sizeClass);
#endif
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < kNumBins);

    // check our bins for a miniheap to reuse
    MiniHeap *existing = _littleheaps[sizeClass].selectForReuse();
    if (existing != nullptr) {
      d_assert(!existing->isMeshed());
      d_assert(!existing->isAttached());
      existing->setAttached(current);
      return existing;
    }

    // if we have objects bigger than the size of a page, allocate
    // multiple pages to amortize the cost of creating a
    // miniheap/globally locking the heap.  For example, asking for
    // 2048 byte objects would allocate 4 4KB pages.
    const size_t objectCount = max(kPageSize / objectSize, kMinStringLen);
    const size_t pageCount = PageCount(objectSize * objectCount);

    auto mh = allocMiniheapLocked(sizeClass, pageCount, objectCount, objectSize);
    d_assert(!mh->isAttached());
    mh->setAttached(current);
    return mh;
  }

  // large, page-multiple allocations
  void *ATTRIBUTE_NEVER_INLINE malloc(size_t sz);

  inline MiniHeap *miniheapForLocked(const void *ptr) const {
    auto mh = reinterpret_cast<MiniHeap *>(Super::lookupMiniheap(ptr));
    __builtin_prefetch(mh, 1, 2);
    return mh;
  }

  inline MiniHeap *miniheapForID(const MiniHeapID id) const {
    auto mh = reinterpret_cast<MiniHeap *>(_mhAllocator.ptrFromOffset(id.value()));
    __builtin_prefetch(mh, 1, 2);
    return mh;
  }

  inline MiniHeapID miniheapIDFor(const MiniHeap *mh) const {
    return MiniHeapID{_mhAllocator.offsetFor(mh)};
  }

  void trackMiniheapLocked(MiniHeap *mh) {
    _littleheaps[mh->sizeClass()].add(mh);
  }

  void untrackMiniheapLocked(MiniHeap *mh) {
    _stats.mhAllocCount -= 1;
    _littleheaps[mh->sizeClass()].remove(mh);
  }

  // called with lock held
  void freeMiniheapAfterMeshLocked(MiniHeap *mh, bool untrack = true) {
    // don't untrack a meshed miniheap -- it has already been untracked
    if (untrack && !mh->isMeshed()) {
      untrackMiniheapLocked(mh);
    }

    mh->MiniHeap::~MiniHeap();
    // memset(reinterpret_cast<char *>(mh), 0x77, sizeof(MiniHeap));
    _mhAllocator.free(mh);
    _miniheapCount--;
  }

  void freeMiniheap(MiniHeap *&mh, bool untrack = true) {
    lock_guard<mutex> lock(_miniheapLock);
    freeMiniheapLocked(mh, untrack);
  }

  void freeMiniheapLocked(MiniHeap *&mh, bool untrack) {
    const auto spanSize = mh->spanSize();
    MiniHeap *toFree[kMaxMeshes];
    size_t last = 0;

    memset(toFree, 0, sizeof(*toFree) * kMaxMeshes);

    // avoid use after frees while freeing
    mh->forEachMeshed([&](MiniHeap *mh) {
      toFree[last++] = mh;
      return false;
    });

    for (size_t i = 0; i < last; i++) {
      MiniHeap *mh = toFree[i];
      const bool isMeshed = mh->isMeshed();
      const auto type = isMeshed ? internal::PageType::Meshed : internal::PageType::Dirty;
      Super::free(reinterpret_cast<void *>(mh->getSpanStart(arenaBegin())), spanSize, type);
      _stats.mhFreeCount++;
      freeMiniheapAfterMeshLocked(mh, untrack);
    }

    mh = nullptr;
  }

  inline void flushBinLocked(size_t sizeClass) {
    auto emptyMiniheaps = _littleheaps[sizeClass].getFreeMiniheaps();
    for (size_t i = 0; i < emptyMiniheaps.size(); i++) {
      freeMiniheapLocked(emptyMiniheaps[i], false);
    }
  }

  void ATTRIBUTE_NEVER_INLINE free(void *ptr);

  inline size_t getSize(void *ptr) const {
    if (unlikely(ptr == nullptr))
      return 0;

    lock_guard<mutex> lock(_miniheapLock);
    // shared_lock<shared_mutex> lock(_miniheapLock);
    auto mh = miniheapForLocked(ptr);
    if (likely(mh)) {
      return mh->objectSize();
    } else {
      return 0;
    }
  }

  inline bool inBounds(void *ptr) const {
    debug("TODO: I don't think this is correct.");

    if (unlikely(ptr == nullptr))
      return false;

    lock_guard<mutex> lock(_miniheapLock);
    // shared_lock<shared_mutex> lock(_miniheapLock);
    auto mh = miniheapForLocked(ptr);
    return mh != nullptr;
  }

  int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

  size_t getAllocatedMiniheapCount() const {
    lock_guard<mutex> lock(_miniheapLock);
    return _miniheapCount;
  }

  void setMeshPeriodNs(std::chrono::nanoseconds period) {
    _meshPeriodNs = period;
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
    if (dst->meshCount() + src->meshCount() > kMaxMeshes) {
      return;
    }

    const size_t dstSpanSize = dst->spanSize();
    const auto dstSpanStart = reinterpret_cast<void *>(dst->getSpanStart(arenaBegin()));

    src->forEachMeshed([&](const MiniHeap *mh) {
      // marks srcSpans read-only
      const auto srcSpan = reinterpret_cast<void *>(mh->getSpanStart(arenaBegin()));
      Super::beginMesh(dstSpanStart, srcSpan, dstSpanSize);
      return false;
    });

    // does the copying of objects and updating of span metadata
    dst->consume(arenaBegin(), src);
    d_assert(src->isMeshed());

    src->forEachMeshed([&](const MiniHeap *mh) {
      d_assert(mh->isMeshed());
      const auto srcSpan = reinterpret_cast<void *>(mh->getSpanStart(arenaBegin()));
      // frees physical memory + re-marks srcSpans as read/write
      Super::finalizeMesh(dstSpanStart, srcSpan, dstSpanSize);
      return false;
    });

    // make sure we adjust what bin the destination is in -- it might
    // now be full and not a candidate for meshing
    _littleheaps[dst->sizeClass()].postFree(dst, dst->inUseCount());
    untrackMiniheapLocked(src);
  }

  inline void ATTRIBUTE_ALWAYS_INLINE maybeMesh() {
    if (!kMeshingEnabled) {
      return;
    }

    if (_meshPeriod == 0) {
      return;
    }

    if (_meshPeriodNs == kZeroNs) {
      return;
    }

    const auto now = std::chrono::high_resolution_clock::now();
    auto duration = chrono::duration_cast<chrono::nanoseconds>(now - _lastMesh);

    if (likely(duration < _meshPeriodNs)) {
      return;
    }

    lock_guard<mutex> lock(_miniheapLock);

    {
      // ensure if two threads tried to grab the mesh lock at the same
      // time, the second one bows out gracefully without meshing
      // twice in a row.
      const auto lockedNow = std::chrono::high_resolution_clock::now();
      auto duration = chrono::duration_cast<chrono::nanoseconds>(lockedNow - _lastMesh);

      if (unlikely(duration < _meshPeriodNs)) {
        return;
      }
    }

    _lastMesh = now;

    meshAllSizeClasses();
  }

  inline bool okToProceed(void *ptr) const {
    lock_guard<mutex> lock(_miniheapLock);

    if (ptr == nullptr)
      return false;

    return miniheapForLocked(ptr) != nullptr;
  }

  inline internal::vector<MiniHeap *> meshingCandidates(int sizeClass) const {
    return _littleheaps[sizeClass].meshingCandidates(kOccupancyCutoff);
  }

private:
  // check for meshes in all size classes -- must be called LOCKED
  void meshAllSizeClasses();

  const size_t _maxObjectSize;
  atomic_size_t _lastMeshEffective{0};
  atomic_size_t _meshPeriod{kDefaultMeshPeriod};

  // always accessed with the mhRWLock exclusively locked
  size_t _miniheapCount{0};

  MWC _fastPrng;

  BinnedTracker<MiniHeap> _littleheaps[kNumBins];

  mutable mutex _miniheapLock{};

  GlobalHeapStats _stats{};

  std::chrono::nanoseconds _meshPeriodNs{kMeshPeriodNs};
  // XXX: should be atomic, but has exception spec?
  std::chrono::time_point<std::chrono::high_resolution_clock> _lastMesh;
};
}  // namespace mesh

#endif  // MESH__GLOBAL_HEAP_H
