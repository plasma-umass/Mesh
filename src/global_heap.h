// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__GLOBAL_HEAP_H
#define MESH__GLOBAL_HEAP_H

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "binnedtracker.h"
#include "internal.h"
#include "meshable-arena.h"
#include "meshing.h"
#include "miniheap.h"

#include "cheap_heap.h"

#include "heaplayers.h"

using namespace HL;

namespace mesh {

class GlobalHeapStats {
public:
  atomic_size_t meshCount;
  atomic_size_t mhFreeCount;
  atomic_size_t mhAllocCount;
  atomic_size_t mhHighWaterMark;
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
    std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);

    for (size_t i = 0; i < kNumBins; i++) {
      _littleheaps[i].printOccupancy();
    }
  }

  inline void dumpStats(int level, bool beDetailed) const {
    if (level < 1)
      return;

    std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);

    debug("MESH COUNT:         %zu\n", (size_t)_stats.meshCount);
    debug("MH Alloc Count:     %zu\n", (size_t)_stats.mhAllocCount);
    debug("MH Free  Count:     %zu\n", (size_t)_stats.mhFreeCount);
    debug("MH High Water Mark: %zu\n", (size_t)_stats.mhHighWaterMark);
    for (size_t i = 0; i < kNumBins; i++)
      _littleheaps[i].dumpStats(beDetailed);
  }

  // must be called with exclusive _mhRWLock held
  inline MiniHeap *ATTRIBUTE_ALWAYS_INLINE allocMiniheap(int sizeClass, size_t pageCount, size_t objectCount,
                                                         size_t objectSize) {
    const size_t spanSize = HL::CPUInfo::PageSize * pageCount;
    d_assert(0 < spanSize);

    // allocate out of the arena
    void *span = Super::malloc(spanSize);
    if (unlikely(span == nullptr))
      abort();

    void *buf = _mhAllocator.alloc();
    d_assert(buf != nullptr);

    // if (spanSize > 4096)
    //   mesh::debug("spana %p(%zu) %p (%zu)", span, spanSize, buf, objectSize);
    MiniHeap *mh = new (buf) MiniHeap(span, objectCount, objectSize, _fastPrng, spanSize);
    Super::assoc(span, mh, pageCount);
    _miniheapCount++;

    if (sizeClass >= 0)
      trackMiniheapLocked(sizeClass, mh);

    _stats.mhAllocCount++;
    //_stats.mhHighWaterMark = max(_miniheaps.size(), _stats.mhHighWaterMark.load());
    //_stats.mhClassHWM[sizeClass] = max(_littleheapCounts[sizeClass], _stats.mhClassHWM[sizeClass].load());

    return mh;
  }

  inline MiniHeap *allocSmallMiniheap(size_t objectSize) {
    std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);

    d_assert(objectSize <= _maxObjectSize);

    // TODO: pass in as a parameter
    const int sizeClass = SizeMap::SizeClass(objectSize);

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
      existing->reattach();
      return existing;
    }

    // if we have objects bigger than the size of a page, allocate
    // multiple pages to amortize the cost of creating a
    // miniheap/globally locking the heap.  For example, asking for
    // 2048 byte objects would allocate 4 4KB pages.
    const size_t objectCount = max(HL::CPUInfo::PageSize / objectSize, kMinStringLen);
    const size_t pageCount = PageCount(objectSize * objectCount);

    auto mh = allocMiniheap(sizeClass, pageCount, objectCount, objectSize);
    mh->reattach();
    return mh;
  }

  // large, page-multiple allocations
  void *malloc(size_t sz);

  inline MiniHeap *miniheapForLocked(const void *ptr) const {
    return reinterpret_cast<MiniHeap *>(Super::lookup(ptr));
  }

  // if the MiniHeap is non-null, its reference count is increased by one
  inline MiniHeap *miniheapFor(const void *ptr) const {
    std::shared_lock<std::shared_timed_mutex> sharedLock(_mhRWLock);

    auto mh = miniheapForLocked(ptr);
    if (mh != nullptr)
      mh->ref();

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
    _mhAllocator.free(mh);
    _miniheapCount--;
  }

  void freeMiniheap(MiniHeap *&mh, bool untrack = true) {
    std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);
    freeMiniheapLocked(mh, untrack);
  }

  void freeMiniheapLocked(MiniHeap *&mh, bool untrack) {
    const auto spans = mh->spans();
    const auto spanSize = mh->spanSize();

    const auto meshCount = mh->meshCount();
    for (size_t i = 0; i < meshCount; i++) {
      Super::free(reinterpret_cast<void *>(spans[i]), spanSize);
    }

    _stats.mhFreeCount++;

    freeMiniheapAfterMeshLocked(mh, untrack);
    mh = nullptr;
  }

  inline void free(void *ptr) {
    auto mh = miniheapFor(ptr);
    freeFrom(mh, ptr);
  }

  inline void freeFrom(MiniHeap *mh, void *ptr) {
    // two possibilities: most likely the ptr is small (and therefor
    // owned by a miniheap), or is a large allocation

    hard_assert(mh != nullptr);

    _lastMeshEffective = 1;
    mh->free(ptr);

    // large objects don't trigger meshing, because they are multiples
    // of the page size
    if (mh->objectSize() > kMaxSize) {
      // we need to grab the exclusive lock here, as the read-only
      // lock we took in miniheapFor has already been released
      freeMiniheap(mh, false);
      return;
    }

    bool shouldConsiderMesh = !mh->isEmpty();
    // unreffed by the bin tracker
    // mh->unref();

    const auto sizeClass = SizeMap::SizeClass(mh->objectSize());

    bool shouldFlush = false;
    {
      // FIXME: inefficient
      std::shared_lock<std::shared_timed_mutex> sharedLock(_mhRWLock);
      // this may free the miniheap -- we can't safely access it after
      // this point.
      shouldFlush = _littleheaps[sizeClass].postFree(mh);
      mh = nullptr;
    }

    if (unlikely(shouldFlush)) {
      std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);

      auto emptyMiniheaps = _littleheaps[sizeClass].getFreeMiniheaps();
      for (size_t i = 0; i < emptyMiniheaps.size(); i++) {
        freeMiniheapLocked(emptyMiniheaps[i], false);
      }
    }

    if (shouldConsiderMesh)
      maybeMesh();
  }

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
    std::shared_lock<std::shared_timed_mutex> sharedLock(_mhRWLock);
    return _miniheapCount;
  }

  void setMeshPeriodSecs(double period) {
    _meshPeriodSecs = period;
  }

  void lock() {
    _mhRWLock.lock();
  }

  void unlock() {
    _mhRWLock.unlock();
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
    dst->ref();
    _littleheaps[SizeMap::SizeClass(dst->objectSize())].postFree(dst);
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
    scavenge();
  }

  inline bool okToProceed(void *ptr) const {
    // call to inBounds won't return until it was able to grab the
    // _mhRWLock, which isn't released until meshing is complete.
    return inBounds(ptr);
  }

  inline void scavenge() {
    Super::scavenge();
  }

protected:
  void performMeshing(internal::vector<std::pair<MiniHeap *, MiniHeap *>> &mergeSets) {
    for (auto &mergeSet : mergeSets) {
      // merge _into_ the one with a larger mesh count, potentiall
      // swapping the order of the pair
      if (std::get<0>(mergeSet)->meshCount() < std::get<1>(mergeSet)->meshCount())
        mergeSet = std::pair<MiniHeap *, MiniHeap *>(std::get<1>(mergeSet), std::get<0>(mergeSet));

      meshLocked(std::get<0>(mergeSet), std::get<1>(mergeSet));
    }
  }

  // check for meshes in all size classes -- must be called unlocked
  void meshAllSizeClasses() {
    std::unique_lock<std::shared_timed_mutex> exclusiveLock(_mhRWLock);

    if (!_lastMeshEffective)
      return;

    _lastMeshEffective = 1;

    // const auto start = std::chrono::high_resolution_clock::now();
    size_t partialCount = 0;

    internal::vector<std::pair<MiniHeap *, MiniHeap *>> mergeSets;

    // first, clear out any free memory we might have
    for (size_t i = 0; i < kNumBins; i++) {
      auto emptyMiniheaps = _littleheaps[i].getFreeMiniheaps();
      for (size_t i = 0; i < emptyMiniheaps.size(); i++) {
        freeMiniheapLocked(emptyMiniheaps[i], false);
      }
    }

    // FIXME: is it safe to have this function not use internal::allocator?
    auto meshFound = function<void(std::pair<MiniHeap *, MiniHeap *> &&)>(
        // std::allocator_arg, internal::allocator,
        [&](std::pair<MiniHeap *, MiniHeap *> &&miniheaps) {
          if (std::get<0>(miniheaps)->isMeshingCandidate() && std::get<0>(miniheaps)->isMeshingCandidate())
            mergeSets.push_back(std::move(miniheaps));
        });

    for (size_t i = 0; i < kNumBins; i++) {
      // method::randomSort(_prng, _littleheapCounts[i], _littleheaps[i], meshFound);
      // method::greedySplitting(_prng, _littleheaps[i], meshFound);
      // method::simpleGreedySplitting(_prng, _littleheaps[i], meshFound);
      partialCount += _littleheaps[i].partialSize();
      method::shiftedSplitting(_prng, _littleheaps[i], meshFound);
    }

    // more than ~ 1 MB saved
    _lastMeshEffective = mergeSets.size() > 256;

    if (mergeSets.size() == 0) {
      // debug("nothing to mesh.");
      return;
    }

    _stats.meshCount += mergeSets.size();

    performMeshing(mergeSets);

    _lastMesh = std::chrono::high_resolution_clock::now();

    // const std::chrono::duration<double> duration = _lastMesh - start;
    // debug("mesh took %f, found %zu", duration.count(), mergeSets.size());
  }

  const size_t _maxObjectSize;
  atomic_size_t _lastMeshEffective{0};
  atomic_size_t _meshPeriod{kDefaultMeshPeriod};

  // always accessed with the mhRWLock exclusively locked
  size_t _miniheapCount{0};

  mt19937_64 _prng;
  MWC _fastPrng;

  CheapHeap<128, kArenaSize / kPageSize> _mhAllocator{};

  BinnedTracker<MiniHeap> _littleheaps[kNumBins];

  mutable std::shared_timed_mutex _mhRWLock{};

  GlobalHeapStats _stats{};

  double _meshPeriodSecs{kMeshPeriodSecs};
  // XXX: should be atomic, but has exception spec?
  std::chrono::time_point<std::chrono::high_resolution_clock> _lastMesh;
};
}  // namespace mesh

#endif  // MESH__GLOBAL_HEAP_H
