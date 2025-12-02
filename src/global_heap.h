// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_GLOBAL_HEAP_H
#define MESH_GLOBAL_HEAP_H

#include <algorithm>
#include <array>
#include <mutex>

#include "internal.h"
#include "meshable_arena.h"
#include "mini_heap.h"

#include "heaplayers.h"

using namespace HL;

namespace mesh {

// Cache-line-padded atomic MiniHeapID to avoid false sharing in _pendingPartialHead array.
// Without padding, multiple size classes share the same cache line, causing severe
// contention when multiple threads perform lock-free operations on different size classes.
struct alignas(CACHELINE_SIZE) CachelinePaddedAtomicMiniHeapID {
  std::atomic<MiniHeapID> head{};
};
static_assert(sizeof(CachelinePaddedAtomicMiniHeapID) == CACHELINE_SIZE,
              "CachelinePaddedAtomicMiniHeapID must be exactly one cache line");
static_assert(alignof(CachelinePaddedAtomicMiniHeapID) == CACHELINE_SIZE,
              "CachelinePaddedAtomicMiniHeapID must be cache-line aligned");

class EpochLock {
private:
  DISALLOW_COPY_AND_ASSIGN(EpochLock);

public:
  EpochLock() {
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE current() const noexcept {
    // Acquire ordering: if we read a value stored with release, we see all
    // operations that happened-before that store. This ensures readers see
    // all meshing work that completed before the epoch was updated.
    return _epoch.load(std::memory_order_acquire);
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE isSame(size_t startEpoch) const noexcept {
    return current() == startEpoch;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE lock() noexcept {
    // Release ordering: all subsequent meshing operations will be ordered
    // after this store. Readers with acquire loads will see this update.
    // The old value is only used for assertion, so relaxed read is fine.
    const auto old = _epoch.fetch_add(1, std::memory_order_release);
    hard_assert(old % 2 == 0);
  }

  inline void ATTRIBUTE_ALWAYS_INLINE unlock() noexcept {
    // Release ordering: all prior meshing operations are ordered before this
    // store. Readers with acquire loads will see all meshing work completed.
#ifndef NDEBUG
    const auto old = _epoch.fetch_add(1, std::memory_order_release);
    d_assert(old % 2 == 1);
#else
    _epoch.fetch_add(1, std::memory_order_release);
#endif
  }

private:
  atomic_size_t _epoch{0};
};

class GlobalHeapStats {
public:
  atomic_size_t meshCount;
  size_t mhFreeCount;
  size_t mhAllocCount;
  size_t mhHighWaterMark;
};

template <size_t PageSize>
class GlobalHeap : public MeshableArena<PageSize> {
private:
  DISALLOW_COPY_AND_ASSIGN(GlobalHeap);
  typedef MeshableArena<PageSize> Super;

public:
  enum { Alignment = 16 };

  static_assert(HL::gcd<MmapHeap::Alignment, Alignment>::value == Alignment,
                "expected MmapHeap to have 16-byte alignment");
  using MiniHeapT = MiniHeap<PageSize>;
  using MiniHeapListEntryT = MiniHeapListEntry<PageSize>;

  // RAII guard to acquire all locks in consistent order (for meshing)
  class AllLocksGuard {
  private:
    DISALLOW_COPY_AND_ASSIGN(AllLocksGuard);
    std::array<mutex, kNumBins> &_locks;
    mutex &_largeLock;
    mutex &_arenaLockRef;

  public:
    AllLocksGuard(std::array<mutex, kNumBins> &locks, mutex &largeLock, mutex &arenaLockRef)
        : _locks(locks), _largeLock(largeLock), _arenaLockRef(arenaLockRef) {
      // Lock ordering: size-classes[0..N-1] -> large -> arena
      // This allows the fast path (reusing miniheaps) to only acquire size-class lock,
      // then optionally acquire arena lock later if new allocation is needed.
      for (size_t i = 0; i < kNumBins; i++) {
        _locks[i].lock();
      }
      _largeLock.lock();
      _arenaLockRef.lock();
    }

    ~AllLocksGuard() {
      // Release in reverse order
      _arenaLockRef.unlock();
      _largeLock.unlock();
      for (size_t i = kNumBins; i > 0; i--) {
        _locks[i - 1].unlock();
      }
    }
  };

  GlobalHeap() : Super(), _maxObjectSize(SizeMap::ByteSizeForClass(kNumBins - 1)), _lastMesh{time::now()} {
  }

  inline void dumpStrings() const {
    AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);

    mesh::debug("TODO: reimplement printOccupancy\n");
    // for (size_t i = 0; i < kNumBins; i++) {
    //   _littleheaps[i].printOccupancy();
    // }
  }

  inline void flushAllBins() {
    for (size_t sizeClass = 0; sizeClass < kNumBins; sizeClass++) {
      flushBinLocked(sizeClass);
    }
  }

  void scavenge(bool force = false) {
    AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);

    Super::scavenge(force);
  }

  void dumpStats(int level, bool beDetailed) const;

  // must be called with _arenaLock AND appropriate size-class lock held
  inline MiniHeapT *ATTRIBUTE_ALWAYS_INLINE allocMiniheapLocked(int sizeClass, size_t pageCount, size_t objectCount,
                                                                size_t objectSize, size_t pageAlignment = 1) {
    d_assert(0 < pageCount);

    void *buf = this->_mhAllocator.alloc();
    d_assert(buf != nullptr);

    // allocate out of the arena
    Span span{0, 0};
    char *spanBegin = Super::pageAlloc(span, pageCount, pageAlignment);
    d_assert(spanBegin != nullptr);
    d_assert((reinterpret_cast<uintptr_t>(spanBegin) / getPageSize()) % pageAlignment == 0);

    MiniHeapT *mh = new (buf) MiniHeapT(this->arenaBegin(), span, objectCount, objectSize);

    const auto miniheapID = MiniHeapID{this->_mhAllocator.offsetFor(buf)};
    Super::trackMiniHeap(span, miniheapID);

    // mesh::debug("%p (%u) created!\n", mh, GetMiniHeapID(mh));

    _miniheapCount++;
    _stats.mhAllocCount++;
    const size_t count = _miniheapCount.load(std::memory_order_relaxed);
    _stats.mhHighWaterMark = max(count, _stats.mhHighWaterMark);

    return mh;
  }

  inline void *pageAlignedAlloc(size_t pageAlignment, size_t pageCount) {
    // if given a very large allocation size (e.g. (uint64_t)-8), it is possible
    // the pageCount calculation overflowed.  An allocation that big is impossible
    // to satisfy anyway, so just fail early.
    if (unlikely(pageCount == 0)) {
      return nullptr;
    }

    // Lock ordering: large alloc lock -> arena lock
    lock_guard<mutex> lock(_largeAllocLock);
    lock_guard<mutex> arenaLock(_arenaLock);

    const size_t pageSize = getPageSize();
    MiniHeapT *mh = allocMiniheapLocked(-1, pageCount, 1, pageCount * pageSize, pageAlignment);

    d_assert(mh->isLargeAlloc());
    d_assert(mh->spanSize() == pageCount * pageSize);
    // d_assert(mh->objectSize() == pageCount * pageSize);

    void *ptr = mh->mallocAt(this->arenaBegin(), 0);

    return ptr;
  }

  inline MiniHeapListEntryT *freelistFor(uint8_t freelistId, int sizeClass) {
    switch (freelistId) {
    case list::Empty:
      return &_emptyFreelist[sizeClass].first;
    case list::Partial:
      return &_partialFreelist[sizeClass].first;
    case list::Full:
      // Full miniheaps are not on any list (lock-free transition path)
      return nullptr;
    }
    // remaining case is 'attached', for which there is no freelist
    return nullptr;
  }

  // Drain the lock-free pending partial list into the actual partial freelist.
  // Must be called with _miniheapLocks[sizeClass] held.
  inline void drainPendingPartialLocked(int sizeClass) {
    MiniHeapID head = _pendingPartialHead[sizeClass].head.exchange(MiniHeapID{}, std::memory_order_acquire);

    while (head.hasValue() && head != list::Head) {
      MiniHeapT *mh = GetMiniHeap<MiniHeapT>(head);
      // Use dedicated _pendingNext field for pending list traversal.
      // This is separate from _freelist._next to prevent races where a processed
      // miniheap is reallocated, freed, and pushed to a NEW pending list (which
      // would overwrite _freelist._next) while we're still iterating the OLD list.
      MiniHeapID next = mh->pendingNext();

      // Clear the pending link
      mh->setPendingNext(MiniHeapID{});

      // Check current state - inUse may have changed since queuing
      auto inUse = mh->inUseCount();
      auto max = mh->maxCount();

      // IMPORTANT: add() sets freelistId BEFORE we clear pending.
      // This prevents races where another thread could push the same miniheap
      // back to pending between clearing pending and changing freelistId.
      // Once freelistId != Full, trySetPendingFromFull will fail.
      if (inUse == 0) {
        _emptyFreelist[sizeClass].first.add(nullptr, list::Empty, list::Head, mh);
        _emptyFreelist[sizeClass].second++;
      } else if (inUse == max) {
        // Rare: became full again. Keep freelistId=Full, but we MUST clear pending
        // before the loop continues. We set freelistId explicitly to mark the
        // transition complete even though it's already Full.
      } else {
        // Common case: add to partial freelist
        _partialFreelist[sizeClass].first.add(nullptr, list::Partial, list::Head, mh);
        _partialFreelist[sizeClass].second++;
      }

      // Clear pending AFTER freelistId is updated. This closes the race window.
      mh->clearPending();

      head = next;
    }
  }

  // Push a miniheap onto the pending partial list (lock-free).
  // Atomically sets pending flag if Full, then pushes to pending list.
  // FreelisId remains Full until drained. If not Full, this is a no-op.
  inline void tryPushPendingPartial(MiniHeapT *mh, int sizeClass) {
    // Atomically set pending flag if Full
    if (!mh->trySetPendingFromFull()) {
      return;
    }

    // Push onto pending list using dedicated _pendingNext field for linking.
    // This is separate from _freelist._next to prevent races during drain iteration.
    MiniHeapID myId = GetMiniHeapID(mh);
    MiniHeapID oldHead = _pendingPartialHead[sizeClass].head.load(std::memory_order_relaxed);
    do {
      mh->setPendingNext(oldHead);
    } while (!_pendingPartialHead[sizeClass].head.compare_exchange_weak(oldHead, myId, std::memory_order_release,
                                                                        std::memory_order_relaxed));
  }

  // Must call drainPendingPartialLocked before this if not already drained.
  inline bool postFreeLocked(MiniHeapT *mh, int sizeClass, size_t inUse) {
    // its possible we raced between reading isAttached + grabbing a lock.
    // just check here to avoid having to play whack-a-mole at each call site.
    if (mh->isAttached()) {
      return false;
    }

    // If miniheap is pending (on lock-free pending list), don't manipulate it.
    // The drain will handle it on next lock acquisition.
    if (mh->isPending()) {
      return false;
    }

    const auto currFreelistId = mh->freelistId();
    auto currFreelist = freelistFor(currFreelistId, sizeClass);
    const auto max = mh->maxCount();

    std::pair<MiniHeapListEntryT, size_t> *list;
    uint8_t newListId;

    if (inUse == 0) {
      // if the miniheap is already in the right list there is nothing to do
      if (currFreelistId == list::Empty) {
        return false;
      }
      newListId = list::Empty;
      list = &_emptyFreelist[sizeClass];
    } else if (inUse == max || !isBelowPartialThreshold(inUse, max)) {
      // Full or above 80% threshold - not on any list
      if (currFreelistId == list::Full) {
        return false;
      }
      // Remove from current list and set to Full state
      if (currFreelist != nullptr) {
        mh->getFreelist()->remove(currFreelist);
      }
      mh->setFreelistId(list::Full);
      // Clear freelist pointers so they're known to be unlinked
      mh->getFreelist()->setNext(MiniHeapID{});
      mh->getFreelist()->setPrev(MiniHeapID{});
      return false;
    } else {
      if (currFreelistId == list::Partial) {
        return false;
      }
      newListId = list::Partial;
      list = &_partialFreelist[sizeClass];
    }

    list->first.add(currFreelist, newListId, list::Head, mh);
    list->second++;

    return _emptyFreelist[sizeClass].second > kBinnedTrackerMaxEmpty;
  }

  inline void releaseMiniheapLocked(MiniHeapT *mh, int sizeClass) {
    // ensure this flag is always set with the miniheap lock held
    mh->unsetAttached();
    const auto inUse = mh->inUseCount();
    postFreeLocked(mh, sizeClass, inUse);
  }

  template <uint32_t Size>
  inline void releaseMiniheaps(FixedArray<MiniHeapT, Size> &miniheaps) {
    if (miniheaps.size() == 0) {
      return;
    }

    // All miniheaps in the array are from the same size class
    const int sizeClass = miniheaps[0]->sizeClass();
    d_assert(sizeClass >= 0 && sizeClass < kNumBins);

    lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
    drainPendingPartialLocked(sizeClass);
    for (auto mh : miniheaps) {
      d_assert(mh->sizeClass() == sizeClass);
      releaseMiniheapLocked(mh, sizeClass);
    }
    miniheaps.clear();
  }

  template <uint32_t Size>
  size_t fillFromList(FixedArray<MiniHeapT, Size> &miniheaps, pid_t current,
                      std::pair<MiniHeapListEntryT, size_t> &freelist, size_t bytesFree) {
    if (freelist.first.empty()) {
      return bytesFree;
    }

    auto nextId = freelist.first.next();
    while (nextId != list::Head && bytesFree < kMiniheapRefillGoalSize && !miniheaps.full()) {
      auto mh = GetMiniHeap<MiniHeapT>(nextId);
      d_assert(mh != nullptr);
      nextId = mh->getFreelist()->next();

      // TODO: we can eventually remove this
      d_assert(!(mh->isFull() || mh->isAttached() || mh->isMeshed()));

      // TODO: this is commented out to match a bug in the previous implementation;
      // it turns out if you don't track bytes free and give more memory to the
      // thread-local cache, things perform better!
      // bytesFree += mh->bytesFree();
      d_assert(!mh->isAttached());
      mh->setAttached(current, freelistFor(mh->freelistId(), mh->sizeClass()));
      d_assert(mh->isAttached() && mh->current() == current);
      hard_assert(!miniheaps.full());
      miniheaps.append(mh);
      d_assert(freelist.second > 0);
      freelist.second--;
    }

    return bytesFree;
  }

  template <uint32_t Size>
  size_t selectForReuse(int sizeClass, FixedArray<MiniHeapT, Size> &miniheaps, pid_t current) {
    size_t bytesFree = fillFromList(miniheaps, current, _partialFreelist[sizeClass], 0);

    if (bytesFree >= kMiniheapRefillGoalSize || miniheaps.full()) {
      return bytesFree;
    }

    // we've exhausted all of our partially full MiniHeaps, but there
    // might still be empty ones we could reuse.
    return fillFromList(miniheaps, current, _emptyFreelist[sizeClass], bytesFree);
  }

  template <uint32_t Size>
  inline void allocSmallMiniheaps(int sizeClass, uint32_t objectSize, FixedArray<MiniHeapT, Size> &miniheaps,
                                  pid_t current) {
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < kNumBins);
    d_assert(objectSize <= _maxObjectSize);

#ifndef NDEBUG
    const size_t classMaxSize = SizeMap::ByteSizeForClass(sizeClass);
    d_assert_msg(objectSize == classMaxSize, "sz(%zu) shouldn't be greater than %zu (class %d)", objectSize,
                 classMaxSize, sizeClass);
#endif

    // Lock ordering: size-class lock -> arena lock
    // We acquire size-class lock first and try to reuse existing miniheaps.
    // Only if we need new miniheaps do we acquire the arena lock.
    lock_guard<mutex> lock(_miniheapLocks[sizeClass]);

    // Drain pending partial list so freed miniheaps are immediately available
    drainPendingPartialLocked(sizeClass);

    for (MiniHeapT *oldMH : miniheaps) {
      releaseMiniheapLocked(oldMH, sizeClass);
    }
    miniheaps.clear();

    d_assert(miniheaps.size() == 0);

    // Fast path: check our bins for a miniheap to reuse (no arena lock needed)
    auto bytesFree = selectForReuse(sizeClass, miniheaps, current);
    if (bytesFree >= kMiniheapRefillGoalSize || miniheaps.full()) {
      return;
    }

    // Slow path: need to allocate new miniheaps, acquire arena lock
    lock_guard<mutex> arenaLock(_arenaLock);

    // if we have objects bigger than the size of a page, allocate
    // multiple pages to amortize the cost of creating a
    // miniheap/globally locking the heap.  For example, asking for
    // 2048 byte objects would allocate 4 4KB pages (or 16KB pages on Apple Silicon).
    // Cap at 1024 to fit within the MiniHeap bitmap size limit (128 bytes = 1024 bits)
    const size_t bitmapLimit = PageSize / kMinObjectSize;
    const size_t objectCount =
        min(max(getPageSize() / objectSize, static_cast<size_t>(kMinStringLen)), static_cast<size_t>(bitmapLimit));
    const size_t pageCount = PageCount(objectSize * objectCount);

    while (bytesFree < kMiniheapRefillGoalSize && !miniheaps.full()) {
      auto mh = allocMiniheapLocked(sizeClass, pageCount, objectCount, objectSize);
      d_assert(!mh->isAttached());
      mh->setAttached(current, freelistFor(mh->freelistId(), sizeClass));
      d_assert(mh->isAttached() && mh->current() == current);
      miniheaps.append(mh);
      bytesFree += mh->bytesFree();
    }

    return;
  }

  // large, page-multiple allocations
  void *ATTRIBUTE_NEVER_INLINE malloc(size_t sz);

  inline MiniHeapT *ATTRIBUTE_ALWAYS_INLINE miniheapForWithEpoch(const void *ptr, size_t &currentEpoch) const {
    currentEpoch = _meshEpoch.current();
    return miniheapFor(ptr);
  }

  inline MiniHeapT *ATTRIBUTE_ALWAYS_INLINE miniheapFor(const void *ptr) const {
    auto mh = reinterpret_cast<MiniHeapT *>(Super::lookupMiniheap(ptr));
    return mh;
  }

  inline MiniHeapT *ATTRIBUTE_ALWAYS_INLINE miniheapForID(const MiniHeapID id) const {
    auto mh = reinterpret_cast<MiniHeapT *>(this->_mhAllocator.ptrFromOffset(id.value()));
    __builtin_prefetch(mh, 1, 2);
    return mh;
  }

  inline MiniHeapID miniheapIDFor(const MiniHeapT *mh) const {
    return MiniHeapID{this->_mhAllocator.offsetFor(mh)};
  }

  void untrackMiniheapLocked(MiniHeapT *mh) {
    // mesh::debug("%p (%u) untracked!\n", mh, GetMiniHeapID(mh));
    _stats.mhAllocCount -= 1;
    mh->getFreelist()->remove(freelistFor(mh->freelistId(), mh->sizeClass()));
  }

  void freeFor(MiniHeapT *mh, void *ptr, size_t startEpoch);

  // called with lock held
  void freeMiniheapAfterMeshLocked(MiniHeapT *mh, bool untrack = true) {
    // don't untrack a meshed miniheap -- it has already been untracked
    if (untrack && !mh->isMeshed()) {
      untrackMiniheapLocked(mh);
    }

    d_assert(!mh->getFreelist()->prev().hasValue());
    d_assert(!mh->getFreelist()->next().hasValue());
    mh->MiniHeapT::~MiniHeap();
    // memset(reinterpret_cast<char *>(mh), 0x77, sizeof(MiniHeap));
    this->_mhAllocator.free(mh);
    _miniheapCount--;
  }

  void freeMiniheap(MiniHeapT *&mh, bool untrack = true) {
    const int sizeClass = mh->sizeClass();
    // Lock ordering: size-class/large lock -> arena lock
    if (sizeClass >= 0) {
      lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
      lock_guard<mutex> arenaLock(_arenaLock);
      freeMiniheapLocked(mh, untrack);
    } else {
      // Large allocation
      lock_guard<mutex> lock(_largeAllocLock);
      lock_guard<mutex> arenaLock(_arenaLock);
      freeMiniheapLocked(mh, untrack);
    }
  }

  // must be called with _arenaLock AND appropriate size-class lock held
  void freeMiniheapLocked(MiniHeapT *&mh, bool untrack) {
    const auto spanSize = mh->spanSize();
    MiniHeapT *toFree[kMaxMeshes];
    size_t last = 0;

    memset(toFree, 0, sizeof(*toFree) * kMaxMeshes);

    // avoid use after frees while freeing
    mh->forEachMeshed([&](MiniHeapT *mh) {
      toFree[last++] = mh;
      return false;
    });

    for (size_t i = 0; i < last; i++) {
      MiniHeapT *mh = toFree[i];
      const bool isMeshed = mh->isMeshed();
      const auto type = isMeshed ? internal::PageType::Meshed : internal::PageType::Dirty;
      Super::free(reinterpret_cast<void *>(mh->getSpanStart(this->arenaBegin())), spanSize, type);
      _stats.mhFreeCount++;
      freeMiniheapAfterMeshLocked(mh, untrack);
    }

    mh = nullptr;
  }

  // flushBinLocked empties _emptyFreelist[sizeClass]
  inline void flushBinLocked(size_t sizeClass) {
    // mesh::debug("flush bin %zu\n", sizeClass);
    d_assert(!_emptyFreelist[sizeClass].first.empty());
    if (_emptyFreelist[sizeClass].first.next() == list::Head) {
      return;
    }

    std::pair<MiniHeapListEntryT, size_t> &empty = _emptyFreelist[sizeClass];
    MiniHeapID nextId = empty.first.next();
    while (nextId != list::Head) {
      auto mh = GetMiniHeap<MiniHeapT>(nextId);
      nextId = mh->getFreelist()->next();
      freeMiniheapLocked(mh, true);
      empty.second--;
    }

    d_assert(empty.first.next() == list::Head);
    d_assert(empty.first.prev() == list::Head);
  }

  void ATTRIBUTE_NEVER_INLINE free(void *ptr);

  inline size_t getSize(void *ptr) const {
    if (unlikely(ptr == nullptr))
      return 0;

    // Look up miniheap first (doesn't require lock)
    auto mh = miniheapFor(ptr);
    if (unlikely(!mh)) {
      return 0;
    }

    const int sizeClass = mh->sizeClass();
    if (sizeClass >= 0) {
      lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
      // Re-verify miniheap is still valid after acquiring lock
      mh = miniheapFor(ptr);
      if (likely(mh)) {
        return mh->objectSize();
      }
    } else {
      // Large allocation
      lock_guard<mutex> lock(_largeAllocLock);
      mh = miniheapFor(ptr);
      if (likely(mh)) {
        return mh->objectSize();
      }
    }
    return 0;
  }

  int mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

  size_t getAllocatedMiniheapCount() const {
    AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);
    return _miniheapCount.load(std::memory_order_relaxed);
  }

  void setMeshPeriodMs(std::chrono::milliseconds period) {
    _meshPeriodMs.store(period, std::memory_order_release);
  }

  void lock() {
    // Acquire all locks in consistent order: size-classes -> large -> arena
    for (size_t i = 0; i < kNumBins; i++) {
      _miniheapLocks[i].lock();
    }
    _largeAllocLock.lock();
    _arenaLock.lock();
  }

  void unlock() {
    // Release in reverse order
    _arenaLock.unlock();
    _largeAllocLock.unlock();
    for (size_t i = kNumBins; i > 0; i--) {
      _miniheapLocks[i - 1].unlock();
    }
  }

  // PUBLIC ONLY FOR TESTING
  // after call to meshLocked() completes src is a nullptr
  void ATTRIBUTE_NEVER_INLINE meshLocked(MiniHeapT *dst, MiniHeapT *&src);

  inline void ATTRIBUTE_ALWAYS_INLINE maybeMesh() {
    if (!kMeshingEnabled) {
      return;
    }

    if (_meshPeriod == 0) {
      return;
    }

    const auto meshPeriodMs = _meshPeriodMs.load(std::memory_order_acquire);
    if (meshPeriodMs == kZeroMs) {
      return;
    }

    const auto now = time::now();
    const auto lastMesh = _lastMesh.load(std::memory_order_acquire);
    auto duration = chrono::duration_cast<chrono::milliseconds>(now - lastMesh);

    if (likely(duration < meshPeriodMs)) {
      return;
    }

    AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);

    {
      // ensure if two threads tried to grab the mesh lock at the same
      // time, the second one bows out gracefully without meshing
      // twice in a row.
      const auto lockedNow = time::now();
      const auto lockedLastMesh = _lastMesh.load(std::memory_order_relaxed);
      auto duration = chrono::duration_cast<chrono::milliseconds>(lockedNow - lockedLastMesh);

      if (unlikely(duration < meshPeriodMs)) {
        return;
      }
    }

    _lastMesh.store(now, std::memory_order_release);

    meshAllSizeClassesLocked();
  }

  inline bool okToProceed(void *ptr) const {
    if (ptr == nullptr) {
      return false;
    }

    // Look up miniheap first (doesn't require lock)
    auto mh = miniheapFor(ptr);
    if (!mh) {
      return false;
    }

    const int sizeClass = mh->sizeClass();
    if (sizeClass >= 0) {
      lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
      return miniheapFor(ptr) != nullptr;
    } else {
      lock_guard<mutex> lock(_largeAllocLock);
      return miniheapFor(ptr) != nullptr;
    }
  }

  inline internal::vector<MiniHeapT *> meshingCandidatesLocked(int sizeClass) const {
    // FIXME: duplicated with code in halfSplit
    internal::vector<MiniHeapT *> bucket{};

    auto nextId = _partialFreelist[sizeClass].first.next();
    while (nextId != list::Head) {
      auto mh = GetMiniHeap<MiniHeapT>(nextId);
      if (mh->isMeshingCandidate() && (mh->fullness() < kOccupancyCutoff)) {
        bucket.push_back(mh);
      }
      nextId = mh->getFreelist()->next();
    }

    return bucket;
  }

private:
  // check for meshes in all size classes -- must be called LOCKED
  void meshAllSizeClassesLocked();
  // meshSizeClassLocked returns the number of merged sets found
  size_t meshSizeClassLocked(size_t sizeClass, MergeSetArray<PageSize> &mergeSets, SplitArray<PageSize> &left,
                             SplitArray<PageSize> &right);

  const size_t _maxObjectSize;
  atomic_size_t _meshPeriod{kDefaultMeshPeriod};
  std::atomic<std::chrono::milliseconds> _meshPeriodMs{kMeshPeriodMs};

  atomic_size_t ATTRIBUTE_ALIGNED(CACHELINE_SIZE) _lastMeshEffective{0};

  // we want this on its own cacheline
  EpochLock ATTRIBUTE_ALIGNED(CACHELINE_SIZE) _meshEpoch{};

  // Atomic count of miniheaps - accessed under different size-class locks
  // Cacheline aligned to avoid sharing cacheline with _meshEpoch
  atomic_size_t ATTRIBUTE_ALIGNED(CACHELINE_SIZE) _miniheapCount{0};

  static constexpr std::pair<MiniHeapListEntryT, size_t> Head{MiniHeapListEntryT{list::Head, list::Head}, 0};

  // these must only be accessed or modified with the appropriate _miniheapLocks[sizeClass] held
  std::array<std::pair<MiniHeapListEntryT, size_t>, kNumBins> _emptyFreelist{
      Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head,
      Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head};
  std::array<std::pair<MiniHeapListEntryT, size_t>, kNumBins> _partialFreelist{
      Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head,
      Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head, Head};

  // Lock-free pending partial list: miniheaps transitioning from Full to Partial
  // are pushed here without holding locks. Drained to _partialFreelist under lock.
  // Each entry is cache-line-padded to avoid false sharing between size classes.
  std::array<CachelinePaddedAtomicMiniHeapID, kNumBins> _pendingPartialHead{};

  // Per-size-class locks to reduce contention on freelists
  mutable std::array<mutex, kNumBins> _miniheapLocks{};
  // Separate lock for large allocations (sizeClass == -1)
  mutable mutex _largeAllocLock{};
  // Lock for shared arena/allocator state (pageAlloc, trackMiniHeap, _mhAllocator)
  mutable mutex _arenaLock{};

  GlobalHeapStats _stats{};

  // XXX: should be atomic, but has exception spec?
  std::atomic<time::time_point> _lastMesh;
};

static_assert(kNumBins == 25, "if this changes, add more 'Head's above");
static_assert(sizeof(std::array<MiniHeapListEntry<4096>, kNumBins>) == kNumBins * 8, "list size is right");
// GlobalHeap size includes: kNumBins * CACHELINE_SIZE for cache-line-padded _pendingPartialHead
static_assert(sizeof(GlobalHeap<4096>) < (kNumBins * 8 * 2 + kNumBins * CACHELINE_SIZE + 64 * 7 + 100000),
              "gh small enough");
}  // namespace mesh

#endif  // MESH_GLOBAL_HEAP_H
