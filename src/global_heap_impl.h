// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifndef MESH_GLOBAL_HEAP_IMPL_H
#define MESH_GLOBAL_HEAP_IMPL_H

#include <utility>

#include "global_heap.h"

#include "meshing.h"
#include "runtime.h"

namespace mesh {

template <typename MiniHeapT>
MiniHeapT *GetMiniHeap(const MiniHeapID id) {
  hard_assert(id.hasValue() && id != list::Head);

  // Extract PageSize from the MiniHeap type at compile time
  constexpr size_t PageSize = MiniHeapT::kPageSize;
  return reinterpret_cast<MiniHeapT *>(runtime<PageSize>().heap().miniheapForID(id));
}

template <typename MiniHeapT>
MiniHeapID GetMiniHeapID(const MiniHeapT *mh) {
  if (unlikely(mh == nullptr)) {
    d_assert(false);
    return MiniHeapID{0};
  }

  // Extract PageSize from the MiniHeap type at compile time
  constexpr size_t PageSize = MiniHeapT::kPageSize;
  return runtime<PageSize>().heap().miniheapIDFor(mh);
}

template <size_t PageSize>
void *GlobalHeap<PageSize>::malloc(size_t sz) {
#ifndef NDEBUG
  if (unlikely(sz <= kMaxSize)) {
    abort();
  }
#endif

  const auto pageCount = PageCount(sz);

  return pageAlignedAlloc(1, pageCount);
}

template <size_t PageSize>
void GlobalHeap<PageSize>::free(void *ptr) {
  size_t startEpoch{0};
  auto mh = miniheapForWithEpoch(ptr, startEpoch);
  if (unlikely(!mh)) {
#ifndef NDEBUG
    if (ptr != nullptr) {
      debug("FIXME: free of untracked ptr %p", ptr);
    }
#endif
    return;
  }
  this->freeFor(mh, ptr, startEpoch);
}

template <size_t PageSize>
void GlobalHeap<PageSize>::freeFor(MiniHeapT *mh, void *ptr, size_t startEpoch) {
  if (unlikely(ptr == nullptr)) {
    return;
  }

  if (unlikely(!mh)) {
    return;
  }

  // large objects are tracked with a miniheap per object and don't
  // trigger meshing, because they are multiples of the page size.
  // This can also include, for example, single page allocations w/
  // 16KB alignment.
  if (mh->isLargeAlloc()) {
    // Lock ordering: large alloc lock -> arena lock
    lock_guard<mutex> lock(_largeAllocLock);
    lock_guard<mutex> arenaLock(_arenaLock);
    freeMiniheapLocked(mh, false);
    return;
  }

  d_assert(mh->maxCount() > 1);

  auto freelistId = mh->freelistId();
  auto isAttached = mh->isAttached();
  auto sizeClass = mh->sizeClass();

  // try to avoid storing to this cacheline; the branch is worth it to avoid
  // multi-threaded contention
  if (_lastMeshEffective.load(std::memory_order::memory_order_acquire) == 0) {
    _lastMeshEffective.store(1, std::memory_order::memory_order_release);
  }
  // read inUseCount before calling free to avoid stalling after the
  // LOCK CMPXCHG in mh->free
  auto remaining = mh->inUseCount() - 1;

  // here can't call mh->free(arenaBegin(), ptr), because in consume takeBitmap always clear the bitmap,
  // if clearIfNotFree after takeBitmap
  // it alwasy return false, but in this case, you need to free again.
  auto wasSet = mh->clearIfNotFree(this->arenaBegin(), ptr);

  bool shouldMesh = false;

  // the epoch will be odd if a mesh was in progress when we looked up
  // the miniheap; if that is true, or a meshing started between then
  // and now we can't be sure the above free was successful
  if (startEpoch % 2 == 1 || !_meshEpoch.isSame(startEpoch)) {
    // a mesh was started in between when we looked up our miniheap
    // and now.  synchronize to avoid races
    d_assert(sizeClass >= 0 && sizeClass < kNumBins);
    lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
    drainPendingPartialLocked(sizeClass);

    const auto origMh = mh;
    mh = miniheapForWithEpoch(ptr, startEpoch);
    if (unlikely(mh == nullptr)) {
      return;
    }

    if (unlikely(mh != origMh)) {
      hard_assert(!mh->isMeshed());
      if (mh->isRelated(origMh) && !wasSet) {
        // we have confirmation that we raced with meshing, so free the pointer
        // on the new miniheap
        d_assert(sizeClass == mh->sizeClass());
        mh->free(this->arenaBegin(), ptr);
      } else {
        // our MiniHeap is unrelated to whatever is here in memory now - get out of here.
        return;
      }
    }

    if (unlikely(mh->sizeClass() != sizeClass || mh->isLargeAlloc())) {
      // TODO: This papers over a bug where the miniheap was freed
      //  + reused out from under us while we were waiting for the mh lock.
      //  It doesn't eliminate the problem (which should be solved
      //  by storing the 'created epoch' on the MiniHeap), but it should
      //  further reduce its probability
      return;
    }

    remaining = mh->inUseCount();
    freelistId = mh->freelistId();
    isAttached = mh->isAttached();

    if (!isAttached && (remaining == 0 || freelistId == list::Full)) {
      // this may free the miniheap -- we can't safely access it after
      // this point.
      postFreeLocked(mh, sizeClass, remaining);
      mh = nullptr;
      // Note: flushBinLocked deferred to next mesh cycle (requires arena lock)
    } else {
      shouldMesh = true;
    }
  } else {
    // the free went through ok; if we _were_ full, or now _are_ empty,
    // make sure to update the littleheaps
    if (!isAttached && (remaining == 0 || freelistId == list::Full)) {
      d_assert(sizeClass >= 0 && sizeClass < kNumBins);

      if (remaining > 0 && freelistId == list::Full) {
        // Lock-free path: Full -> Partial transition
        // Only push to partial list when occupancy drops below 80% threshold.
        // Use pre-computed 'remaining' to avoid extra atomic read after clearIfNotFree().
        // Threshold fuzziness is acceptable: if concurrent frees cause us to miss
        // the exact crossing, the next free will catch it.
        if (isBelowPartialThreshold(remaining, mh->maxCount())) {
          tryPushPendingPartial(mh, sizeClass);
        }
        shouldMesh = true;
      } else {
        // remaining == 0: need lock for Empty transition
        lock_guard<mutex> lock(_miniheapLocks[sizeClass]);
        drainPendingPartialLocked(sizeClass);

        // there are 2 ways we could have raced with meshing:
        //
        // 1. when writing to the MiniHeap's bitmap (which we check for
        //    above with the !_meshEpoch.isSame(current)).  this is what
        //    the outer if statement here takes care of.
        //
        // 2. this thread losing the race with acquiring _miniheapLocks[sizeClass]
        //    (what we care about here).  for thi case, we know a) our
        //    write to the MiniHeap's bitmap succeeded (or we would be
        //    in the other side of the outer if statement), and b) our
        //    MiniHeap could have been freed from under us while we were
        //    waiting for this lock (if e.g. remaining == 0, a mesh
        //    happened on another thread, and the other thread notices
        //    this MiniHeap is empty (b.c. an empty MiniHeap meshes with
        //    every other MiniHeap).  We need to be careful here.

        const auto origMh = mh;
        // we have to reload the miniheap here because of the
        // just-described possible race
        mh = miniheapForWithEpoch(ptr, startEpoch);

        // if the MiniHeap associated with the ptr we freed has changed,
        // there are a few possibilities.
        if (unlikely(mh != origMh)) {
          // another thread took care of freeing this MiniHeap for us,
          // super!  nothing else to do.
          if (mh == nullptr) {
            return;
          }

          // check to make sure the new MiniHeap is related (via a
          // meshing relationship) to the one we had before grabbing the
          // lock.
          if (!mh->isRelated(origMh)) {
            // the original miniheap was freed and a new (unrelated)
            // Miniheap allocated for the address space.  nothing else
            // for us to do.
            return;
          } else {
            // TODO: we should really store 'created epoch' on mh and
            // check those are the same here, too.
          }
        }

        if (unlikely(mh->sizeClass() != sizeClass || mh->isLargeAlloc())) {
          // TODO: This papers over a bug where the miniheap was freed
          //  + reused out from under us while we were waiting for the mh lock.
          //  It doesn't eliminate the problem (which should be solved
          //  by storing the 'created epoch' on the MiniHeap), but it should
          //  further reduce its probability
          return;
        }

        // a lot could have happened between when we read this without
        // the lock held and now; just recalculate it.
        remaining = mh->inUseCount();
        postFreeLocked(mh, sizeClass, remaining);
        // Note: flushBinLocked deferred to next mesh cycle (requires arena lock)
      }
    } else {
      shouldMesh = !isAttached;
    }
  }

  if (shouldMesh) {
    maybeMesh();
  }
}

template <size_t PageSize>
int GlobalHeap<PageSize>::mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  if (!oldp || !oldlenp || *oldlenp < sizeof(size_t))
    return -1;

  auto statp = reinterpret_cast<size_t *>(oldp);

  // Handle operations that need special lock handling first
  if (strcmp(name, "mesh.scavenge") == 0) {
    // scavenge() acquires locks internally
    scavenge(true);
    return 0;
  } else if (strcmp(name, "mesh.compact") == 0) {
    // Acquire all locks for meshing, then release for scavenge
    {
      AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);
      meshAllSizeClassesLocked();
    }
    // scavenge() acquires locks internally
    scavenge(true);
    return 0;
  }

  // All other operations need locks held
  AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);

  if (strcmp(name, "mesh.check_period") == 0) {
    *statp = _meshPeriod;
    if (!newp || newlen < sizeof(size_t))
      return -1;
    auto newVal = reinterpret_cast<size_t *>(newp);
    _meshPeriod = *newVal;
    // resetNextMeshCheck();
  } else if (strcmp(name, "mesh.barrier") == 0) {
    // read-only: 0 = mprotect, 1 = userfaultfd (user-mode only), 2 = userfaultfd
    *statp = static_cast<size_t>(Super::writeBarrier().mode());
  } else if (strcmp(name, "mesh.pass_count") == 0) {
    *statp = _stats.meshPassCount;
  } else if (strcmp(name, "mesh.pass_total_ns") == 0) {
    *statp = _stats.meshPassTotalNs;
  } else if (strcmp(name, "mesh.pass_max_ns") == 0) {
    // longest mesh pass: the longest pause meshing imposes on allocating threads
    *statp = _stats.meshPassMaxNs;
  } else if (strcmp(name, "mesh.pairs_deferred") == 0) {
    // meshable pairs skipped because a span's occupancy was still changing
    *statp = _stats.meshPairsDeferred;
  } else if (strcmp(name, "mesh.vma_count") == 0) {
    // kernel mappings the arena occupies (upper bound)
    *statp = Super::arenaVmaCount();
  } else if (strcmp(name, "mesh.vma_limit") == 0) {
    // the most the arena may occupy before meshing stops (0: page-based cap)
    *statp = Super::arenaVmaLimit();
  } else if (strcmp(name, "mesh.barrier_stalls") == 0) {
    // writes that hit a span while it was being meshed
    *statp = Super::writeBarrier().stats().stalls;
  } else if (strcmp(name, "mesh.barrier_max_window_ns") == 0) {
    // longest time a span was write-protected (upper bound on a stall)
    *statp = Super::writeBarrier().stats().maxWindowNs;
  } else if (strcmp(name, "arena") == 0) {
    // not sure what this should do
  } else if (strcmp(name, "stats.resident") == 0) {
    auto pss = internal::measurePssKiB();
    // mesh::debug("measurePssKiB: %zu KiB", pss);

    *statp = pss * 1024;  // originally in KB
  } else if (strcmp(name, "stats.active") == 0) {
    // all miniheaps at least partially full
    size_t sz = 0;
    // for (size_t i = 0; i < kNumBins; i++) {
    //   const auto count = _littleheaps[i].nonEmptyCount();
    //   if (count == 0)
    //     continue;
    //   sz += count * _littleheaps[i].objectSize() * _littleheaps[i].objectCount();
    // }
    *statp = sz;
  } else if (strcmp(name, "stats.allocated") == 0) {
    // TODO: revisit this
    // same as active for us, for now -- memory not returned to the OS
    size_t sz = 0;
    for (size_t i = 0; i < kNumBins; i++) {
      // const auto &bin = _littleheaps[i];
      // const auto count = bin.nonEmptyCount();
      // if (count == 0)
      //   continue;
      // sz += bin.objectSize() * bin.allocatedObjectCount();
    }
    *statp = sz;
  }
  return 0;
}

template <size_t PageSize>
void GlobalHeap<PageSize>::meshLocked(MiniHeapT *dst, MiniHeapT *&src) {
  // mesh::debug("mesh dst:%p <- src:%p\n", dst, src);
  // dst->dumpDebug();
  // src->dumpDebug();
  const size_t dstSpanSize = dst->spanSize();
  const auto dstSpanStart = reinterpret_cast<void *>(dst->getSpanStart(this->arenaBegin()));

  src->forEachMeshed([&](const MiniHeapT *mh) {
    // marks srcSpans read-only
    const auto srcSpan = reinterpret_cast<void *>(mh->getSpanStart(this->arenaBegin()));
    Super::beginMesh(dstSpanStart, srcSpan, dstSpanSize);
    return false;
  });

  if (unlikely(_meshHookForTesting != nullptr)) {
    _meshHookForTesting(_meshHookArg);
  }

  // does the copying of objects and updating of span metadata
  dst->consume(this->arenaBegin(), src);
  d_assert(src->isMeshed());

  src->forEachMeshed([&](const MiniHeapT *mh) {
    d_assert(mh->isMeshed());
    const auto srcSpan = reinterpret_cast<void *>(mh->getSpanStart(this->arenaBegin()));
    // frees physical memory + re-marks srcSpans as read/write
    Super::finalizeMesh(dstSpanStart, srcSpan, dstSpanSize);
    return false;
  });
  Super::freePhys(reinterpret_cast<void *>(src->getSpanStart(this->arenaBegin())), dstSpanSize);

  // make sure we adjust what bin the destination is in -- it might
  // now be full and not a candidate for meshing
  postFreeLocked(dst, dst->sizeClass(), dst->inUseCount());
  untrackMiniheapLocked(src);
}

template <size_t PageSize>
size_t GlobalHeap<PageSize>::meshSizeClassLocked(size_t sizeClass, MergeSetArray<PageSize> &mergeSets,
                                                 SplitArray<PageSize> &left, SplitArray<PageSize> &right,
                                                 PassBudget &budget) {
  size_t mergeSetCount = 0;
  // memset(reinterpret_cast<void *>(&mergeSets), 0, sizeof(mergeSets));
  // memset(&left, 0, sizeof(left));
  // memset(&right, 0, sizeof(right));

  auto meshFound =
      function<bool(std::pair<MiniHeapT *, MiniHeapT *> &&)>([&](std::pair<MiniHeapT *, MiniHeapT *> &&miniheaps) {
        if (miniheaps.first->isMeshingCandidate() && miniheaps.second->isMeshingCandidate()) {
          mergeSets[mergeSetCount] = std::move(miniheaps);
          mergeSetCount++;
        }
        return mergeSetCount < kMaxMergeSets;
      });

  method::shiftedSplitting(this->_fastPrng, &_partialFreelist[sizeClass].first, left, right, meshFound);

  if (mergeSetCount == 0) {
    // debug("nothing to mesh.");
    return 0;
  }

  size_t meshCount = 0;

  // after kMaxMeshDeferral of only unsettled pairs, mesh them anyway
  const bool starving = _unsettled[sizeClass] && time::now() - _unsettledSince[sizeClass] >= kMaxMeshDeferral;
  size_t deferred = 0;

  for (size_t i = 0; i < mergeSetCount && budget.allows(); i++) {
    std::pair<MiniHeapT *, MiniHeapT *> &mergeSet = mergeSets[i];
    MiniHeapT *dst = mergeSet.first;
    MiniHeapT *src = mergeSet.second;
    d_assert(dst != nullptr);
    d_assert(src != nullptr);

    // merge _into_ the one with a larger mesh count, potentially
    // swapping the order of the pair
    const auto dstCount = dst->meshCount();
    const auto srcCount = src->meshCount();
    if (dstCount + srcCount > kMaxMeshes) {
      continue;
    }
    if (dstCount < srcCount) {
      std::swap(dst, src);
    }

    // final check: if one of these miniheaps is now empty
    // (e.g. because a parallel thread is freeing a bunch of objects
    // in a row) save ourselves some work by just tracking this as a
    // regular postFree
    auto oneEmpty = false;
    if (dst->inUseCount() == 0) {
      postFreeLocked(dst, sizeClass, 0);
      oneEmpty = true;
    }
    if (src->inUseCount() == 0) {
      postFreeLocked(src, sizeClass, 0);
      oneEmpty = true;
    }

    if (oneEmpty) {
      continue;
    }

    // still emptying (or new): mesh it once it settles
    if (!starving && !(settled(dst) && settled(src))) {
      deferred++;
      continue;
    }

    // every span of src's chain gets remapped onto dst
    if (!this->meshWouldExceedMapLimit(src->meshCount())) {
      meshLocked(dst, src);
      meshCount++;
    }
  }

  if (deferred > 0) {
    _stats.meshPairsDeferred.fetch_add(deferred, std::memory_order_relaxed);
  }
  if (deferred > 0 && meshCount == 0) {
    if (!_unsettled[sizeClass]) {
      _unsettled[sizeClass] = true;
      _unsettledSince[sizeClass] = time::now();
    }
  } else {
    _unsettled[sizeClass] = false;
  }

  // remember every partial span's occupancy, for the next pass to compare
  for (MiniHeapID id = _partialFreelist[sizeClass].first.next(); id != list::Head;) {
    const auto mh = GetMiniHeap<MiniHeapT>(id);
    _lastInUse[id.value()] = static_cast<uint16_t>(mh->inUseCount() + 1);
    id = mh->getFreelist()->next();
  }

  // flush things once more (since we may have called postFree instead
  // of mesh above)
  flushBinLocked(sizeClass);

  return meshCount;
}

template <size_t PageSize>
void GlobalHeap<PageSize>::meshAllSizeClassesLocked() {
  // account the whole pass, including the scavenges and early returns.
  // (time::now() is CLOCK_MONOTONIC_COARSE -- scheduler-tick resolution,
  // fine for rate limiting but too coarse to time a pass.)
  struct PassTimer {
    GlobalHeapStats &stats;
    const std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    ~PassTimer() {
      const size_t ns =
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
      stats.meshPassCount.fetch_add(1, std::memory_order_relaxed);
      stats.meshPassTotalNs.fetch_add(ns, std::memory_order_relaxed);
      if (ns > stats.meshPassMaxNs.load(std::memory_order_relaxed)) {
        stats.meshPassMaxNs.store(ns, std::memory_order_relaxed);
      }
    }
  } passTimer{_stats};

  static MergeSetArray<PageSize> *MergeSetsPtr = []() {
    void *ptr =
        mmap(nullptr, sizeof(MergeSetArray<PageSize>), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    hard_assert(ptr != MAP_FAILED);
    return new (ptr) MergeSetArray<PageSize>();
  }();
  static MergeSetArray<PageSize> &MergeSets = *MergeSetsPtr;
  // static_assert(sizeof(MergeSets) == sizeof(void *) * 2 * 4096, "array too big");
  d_assert((reinterpret_cast<uintptr_t>(&MergeSets) & (getPageSize() - 1)) == 0);

  static SplitArray<PageSize> *LeftPtr = []() {
    void *ptr = mmap(nullptr, sizeof(SplitArray<PageSize>), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    hard_assert(ptr != MAP_FAILED);
    return new (ptr) SplitArray<PageSize>();
  }();
  static SplitArray<PageSize> &Left = *LeftPtr;

  static SplitArray<PageSize> *RightPtr = []() {
    void *ptr = mmap(nullptr, sizeof(SplitArray<PageSize>), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    hard_assert(ptr != MAP_FAILED);
    return new (ptr) SplitArray<PageSize>();
  }();
  static SplitArray<PageSize> &Right = *RightPtr;

  // static_assert(sizeof(Left) == sizeof(void *) * 16384, "array too big");
  // static_assert(sizeof(Right) == sizeof(void *) * 16384, "array too big");
  d_assert((reinterpret_cast<uintptr_t>(&Left) & (getPageSize() - 1)) == 0);
  d_assert((reinterpret_cast<uintptr_t>(&Right) & (getPageSize() - 1)) == 0);

  const uint64_t passStartNs = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count());

  // frees during the pass must not trigger scavenges of their own
  Super::setInMeshPass(true);
  struct PassEnd {
    GlobalHeap &heap;
    ~PassEnd() {
      heap.Super::setInMeshPass(false);
    }
  } passEnd{*this};

  // Free empty miniheaps first, on every pass -- including passes that will
  // not mesh because meshing is ineffective or at the mesh cap.  (Returning
  // before this used to strand empty spans once the cap was reached.)
  for (size_t sizeClass = 0; sizeClass < kNumBins; sizeClass++) {
    drainPendingPartialLocked(sizeClass);
    flushBinLocked(sizeClass);
  }

  // release those pages, and reset freed meshed spans to the identity
  // mapping, which also returns their VMAs under the cap
  Super::scavenge(true);

  if (!_lastMeshEffective.load(std::memory_order::memory_order_acquire)) {
    return;
  }

  if (Super::aboveMeshThreshold()) {
    return;
  }

  lock_guard<EpochLock> epochLock(_meshEpoch);

  Super::beginMeshPass();

  // Meshing stops once the pass budget runs out; the next pass resumes at
  // the size class we stopped in.  Besides bounding the pause, pacing keeps
  // a burst of meshing from spending the whole mesh cap on spans that are
  // still emptying (e.g. while an application evicts a cache).
  PassBudget budget;
  budget.deadlineNs = _passBudgetNs ? passStartNs + _passBudgetNs : 0;

  size_t totalMeshCount = 0;
  const size_t deferredBefore = _stats.meshPairsDeferred.load(std::memory_order_relaxed);
  const size_t first = _nextMeshSizeClass;
  size_t visited = 0;
  for (; visited < kNumBins && budget.allows(); visited++) {
    totalMeshCount += meshSizeClassLocked((first + visited) % kNumBins, MergeSets, Left, Right, budget);
  }
  // a class cut off mid-way is redone first
  _nextMeshSizeClass = (first + (budget.exhausted && visited > 0 ? visited - 1 : visited)) % kNumBins;

  Super::endMeshPass();

  madvise(&Left, sizeof(Left), MADV_DONTNEED);
  madvise(&Right, sizeof(Right), MADV_DONTNEED);
  madvise(&MergeSets, sizeof(MergeSets), MADV_DONTNEED);

  // a pass cut short by its budget, or that deferred unsettled pairs, left
  // work behind: keep meshing.  (Otherwise passes stop until the next free --
  // and once an application stops freeing, deferred pairs would never mesh.)
  const bool deferredAny = _stats.meshPairsDeferred.load(std::memory_order_relaxed) != deferredBefore;
  _lastMeshEffective = totalMeshCount > 256 || budget.exhausted || deferredAny;
  _stats.meshCount += totalMeshCount;

  Super::scavenge(true);

  _lastMesh.store(time::now(), std::memory_order_release);
}

// resident pages of [ptr, ptr + pages * pageSize), per mincore (shmem page cache)
inline size_t residentPages(void *ptr, size_t pages, size_t pageSize) {
#ifdef __APPLE__
  static char vec[4096];  // mincore(2) takes char * on macOS
#else
  static unsigned char vec[4096];
#endif
  size_t resident = 0;
  char *p = reinterpret_cast<char *>(ptr);
  while (pages > 0) {
    const size_t n = pages < sizeof(vec) ? pages : sizeof(vec);
    if (mincore(p, n * pageSize, vec) != 0) {
      return resident;
    }
    for (size_t i = 0; i < n; i++) {
      resident += vec[i] & 1;
    }
    p += n * pageSize;
    pages -= n;
  }
  return resident;
}

// Classify every resident arena page: live spans by how full they are and by
// size class, large allocations, and free spans not yet returned to the OS.
// "free" = resident bytes in live spans not holding an object (size-class
// rounding is not included: objects count at their size class).
template <size_t PageSize>
void GlobalHeap<PageSize>::dumpAccounting() const {
  struct Bucket {
    size_t spans, residentPages, inUseBytes, meshed;
  };
  // by occupancy: empty, (0,25%), [25,50%), [50,75%), [75,100%), full
  Bucket occ[6] = {}, large{};
  Bucket bySize[kNumBins] = {};
  size_t aliasPages = 0;
  for (size_t off = 0; off < Super::arenaEndPages(); off++) {
    auto mh = reinterpret_cast<const MiniHeapT *>(Super::miniheapForArenaOffset(off));
    if (mh == nullptr) {
      continue;
    }
    const Span span = mh->span();
    if (off != span.offset) {
      if (off < span.offset || off >= span.offset + span.length) {
        aliasPages++;  // a meshed virtual span aliasing mh's physical span
      }
      continue;
    }
    const size_t resident = residentPages(Super::pagePointer(span.offset), span.length, PageSize);
    if (mh->isLargeAlloc()) {
      large.spans++;
      large.residentPages += resident;
      continue;
    }
    const uint32_t inUse = mh->inUseCount(), max = mh->maxCount();
    const int i = inUse == 0            ? 0
                  : inUse == max        ? 5
                  : inUse * 4 < max     ? 1
                  : inUse * 2 < max     ? 2
                  : inUse * 4 < max * 3 ? 3
                                        : 4;
    for (Bucket *b : {&occ[i], &bySize[mh->sizeClass()]}) {
      b->spans++;
      b->residentPages += resident;
      b->inUseBytes += static_cast<size_t>(inUse) * mh->objectSize();
      b->meshed += mh->meshCount() > 1;
    }
  }
  size_t freePages[2] = {}, freeResident[2] = {};
  Super::forEachFreeSpan([&](const Span &s, bool dirty) {
    freePages[dirty] += s.length;
    freeResident[dirty] += residentPages(Super::pagePointer(s.offset), s.length, PageSize);
  });

  const double mib = static_cast<double>(PageSize) / (1 << 20);
  auto line = [&](const char *what, const Bucket &b) {
    const double res = b.residentPages * mib, used = b.inUseBytes / 1048576.0;
    debug("ACCT %-14s spans=%-7zu resident=%7.1f MiB  objects=%7.1f MiB  free=%6.1f MiB  meshed=%zu\n", what, b.spans,
          res, used, res - used, b.meshed);
  };
  const char *names[6] = {"empty", "<25% full", "25-50% full", "50-75% full", "75-100% full", "full"};
  size_t residentTotal = large.residentPages + freeResident[0] + freeResident[1];
  size_t objectBytes = 0;
  for (int i = 0; i < 6; i++) {
    line(names[i], occ[i]);
    residentTotal += occ[i].residentPages;
    objectBytes += occ[i].inUseBytes;
  }
  debug("ACCT large          spans=%-7zu resident=%7.1f MiB\n", large.spans, large.residentPages * mib);
  debug("ACCT free spans     dirty=%.1f MiB (resident %.1f)  clean=%.1f MiB (resident %.1f)\n", freePages[1] * mib,
        freeResident[1] * mib, freePages[0] * mib, freeResident[0] * mib);
  debug("ACCT meshed aliases %.1f MiB virtual\n", aliasPages * mib);
  for (size_t c = 0; c < kNumBins; c++) {
    if (bySize[c].spans == 0) {
      continue;
    }
    char what[32];
    snprintf(what, sizeof(what), "class %zuB", SizeMap::ByteSizeForClass(c));
    line(what, bySize[c]);
  }
  debug("ACCT total resident=%.1f MiB  small objects=%.1f MiB\n", residentTotal * mib, objectBytes / 1048576.0);
}

template <size_t PageSize>
void GlobalHeap<PageSize>::dumpStats(int level, bool beDetailed) const {
  if (level < 1)
    return;

  AllLocksGuard allLocks(_miniheapLocks, _largeAllocLock, _arenaLock);

  const auto meshedPageHWM = this->meshedPageHighWaterMark();

  debug("MESH COUNT:         %zu\n", (size_t)_stats.meshCount);
  debug("Mesh passes:        %zu (total %.1f ms, max %.2f ms; %zu pairs deferred until settled)\n",
        (size_t)_stats.meshPassCount, _stats.meshPassTotalNs / 1e6, _stats.meshPassMaxNs / 1e6,
        (size_t)_stats.meshPairsDeferred);
  if (Super::arenaVmaLimit() != 0) {
    debug("Arena VMAs:         %zu (limit %zu of vm.max_map_count %zu)\n", Super::arenaVmaCount(),
          Super::arenaVmaLimit(), Super::maxMapCount());
  }
  debug("Meshed MB (total):  %.1f\n", (size_t)_stats.meshCount * (double)PageSize / 1024.0 / 1024.0);
  debug("Meshed pages HWM:   %zu\n", meshedPageHWM);
  debug("Meshed MB HWM:      %.1f\n", meshedPageHWM * (double)PageSize / 1024.0 / 1024.0);
  // debug("Peak RSS reduction: %.2f\n", rssSavings);
  debug("MH Alloc Count:     %zu\n", (size_t)_stats.mhAllocCount);
  debug("MH Free  Count:     %zu\n", (size_t)_stats.mhFreeCount);
  debug("MH High Water Mark: %zu\n", (size_t)_stats.mhHighWaterMark);
  {
    const auto &barrier = Super::writeBarrier();
    const auto bs = barrier.stats();
    debug("Write barrier:      %s\n", barrierModeName(barrier.mode()));
    debug("Barrier stalls:     %zu (in %zu meshed spans)\n", bs.stalls, bs.meshes);
    debug("Barrier window:     max %.1f us, mean %.1f us\n", bs.maxWindowNs / 1000.0,
          bs.meshes ? bs.totalWindowNs / 1000.0 / bs.meshes : 0.0);
  }
  if (level > 1) {
    dumpAccounting();
  }
}

namespace method {

template <size_t PageSize>
void ATTRIBUTE_NEVER_INLINE halfSplit(MWC &prng, MiniHeapListEntry<PageSize> *miniheaps, SplitArray<PageSize> &left,
                                      size_t &leftSize, SplitArray<PageSize> &right, size_t &rightSize) noexcept {
  d_assert(leftSize == 0);
  d_assert(rightSize == 0);
  MiniHeapID mhId = miniheaps->next();
  while (mhId != list::Head && leftSize < kMaxSplitListSize && rightSize < kMaxSplitListSize) {
    auto mh = GetMiniHeap<MiniHeap<PageSize>>(mhId);
    mhId = mh->getFreelist()->next();

    if (!mh->isMeshingCandidate() || (mh->fullness() >= kOccupancyCutoff)) {
      continue;
    }

    if (leftSize <= rightSize) {
      left[leftSize] = mh;
      leftSize++;
    } else {
      right[rightSize] = mh;
      rightSize++;
    }
  }

  internal::mwcShuffle(&left[0], &left[leftSize], prng);
  internal::mwcShuffle(&right[0], &right[rightSize], prng);
}

template <size_t PageSize>
void ATTRIBUTE_NEVER_INLINE shiftedSplitting(
    MWC &prng, MiniHeapListEntry<PageSize> *miniheaps, SplitArray<PageSize> &left, SplitArray<PageSize> &right,
    const function<bool(std::pair<MiniHeap<PageSize> *, MiniHeap<PageSize> *> &&)> &meshFound) noexcept {
  constexpr size_t t = 64;

  if (miniheaps->empty()) {
    return;
  }

  size_t leftSize = 0;
  size_t rightSize = 0;

  halfSplit<PageSize>(prng, miniheaps, left, leftSize, right, rightSize);

  if (leftSize == 0 || rightSize == 0) {
    return;
  }

  // Bitmap size increased from 32 bytes (256 bits) to 128 bytes (1024 bits)
  // Using PageSize to calculate nBytes
  constexpr size_t nBytes = PageSize / kMinObjectSize / 8;
  const size_t limit = rightSize < t ? rightSize : t;
  d_assert(nBytes == left[0]->bitmap().byteCount());

  size_t foundCount = 0;
  for (size_t j = 0; j < leftSize; j++) {
    const size_t idxLeft = j;
    size_t idxRight = j;

    for (size_t i = 0; i < limit; i++, idxRight++) {
      if (unlikely(idxRight >= rightSize)) {
        idxRight %= rightSize;
      }
      auto h1 = left[idxLeft];
      auto h2 = right[idxRight];

      if (h1 == nullptr || h2 == nullptr)
        continue;

      const auto bitmap1 = h1->bitmap().bits();
      const auto bitmap2 = h2->bitmap().bits();

      const bool areMeshable = mesh::bitmapsMeshable(bitmap1, bitmap2, nBytes);

      if (unlikely(areMeshable)) {
        std::pair<MiniHeap<PageSize> *, MiniHeap<PageSize> *> heaps{h1, h2};
        bool shouldContinue = meshFound(std::move(heaps));
        left[idxLeft] = nullptr;
        right[idxRight] = nullptr;
        foundCount++;
        if (unlikely(foundCount > kMaxMeshesPerIteration || !shouldContinue)) {
          return;
        }
      }
    }
  }
}

}  // namespace method
}  // namespace mesh

#endif  // MESH_GLOBAL_HEAP_IMPL_H
