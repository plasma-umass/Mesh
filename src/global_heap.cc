// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <utility>

#include "global_heap.h"

#include "meshing.h"
#include "runtime.h"

namespace mesh {

MiniHeap *GetMiniHeap(const MiniHeapID id) {
  hard_assert(id.hasValue() && id != list::Head);

  return runtime().heap().miniheapForID(id);
}

MiniHeapID GetMiniHeapID(const MiniHeap *mh) {
  if (unlikely(mh == nullptr)) {
    d_assert(false);
    return MiniHeapID{0};
  }

  return runtime().heap().miniheapIDFor(mh);
}

void *GlobalHeap::malloc(size_t sz) {
#ifndef NDEBUG
  if (unlikely(sz <= kMaxSize)) {
    abort();
  }
#endif

  const auto pageCount = PageCount(sz);

  return pageAlignedAlloc(1, pageCount);
}

void GlobalHeap::free(void *ptr) {
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

void GlobalHeap::freeFor(MiniHeap *mh, void *ptr, size_t startEpoch) {
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
    lock_guard<mutex> lock(_miniheapLock);
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
  mh->free(arenaBegin(), ptr);

  bool shouldMesh = false;

  // the epoch will be odd if a mesh was in progress when we looked up
  // the miniheap; if that is true, or a meshing started between then
  // and now we can't be sure the above free was successful
  if (startEpoch % 2 == 1 || !_meshEpoch.isSame(startEpoch)) {
    // a mesh was started in between when we looked up our miniheap
    // and now.  synchronize to avoid races
    lock_guard<mutex> lock(_miniheapLock);

    const auto origMh = mh;
    mh = miniheapForWithEpoch(ptr, startEpoch);

    if (unlikely(mh != origMh)) {
      hard_assert(!mh->isMeshed());
      mh->free(arenaBegin(), ptr);
    }

    remaining = mh->inUseCount();
    freelistId = mh->freelistId();
    isAttached = mh->isAttached();

    if (!isAttached && (remaining == 0 || freelistId == list::Full)) {
      // this may free the miniheap -- we can't safely access it after
      // this point.
      const bool shouldFlush = postFreeLocked(mh, sizeClass, remaining);
      mh = nullptr;
      if (unlikely(shouldFlush)) {
        flushBinLocked(sizeClass);
      }
    } else {
      shouldMesh = true;
    }
  } else {
    // the free went through ok; if we _were_ full, or now _are_ empty,
    // make sure to update the littleheaps
    if (!isAttached && (remaining == 0 || freelistId == list::Full)) {
      lock_guard<mutex> lock(_miniheapLock);

      // there are 2 ways we could have raced with meshing:
      //
      // 1. when writing to the MiniHeap's bitmap (which we check for
      //    above with the !_meshEpoch.isSame(current)).  this is what
      //    the outer if statement here takes care of.
      //
      // 2. this thread losing the race with acquiring _miniheapLock
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
        auto origFound = false;
        mh->forEachMeshed([&](const MiniHeap *eachMh) {
          const auto found = eachMh == origMh;
          origFound = found;
          return found;
        });

        if (!origFound) {
          // the original miniheap was freed and a new (unrelated)
          // Miniheap allocated for the address space.  nothing else
          // for us to do.
          return;
        } else {
          // TODO: we should really store 'created epoch' on mh and
          // check those are the same here, too.
        }
      }

      // a lot could have happened between when we read this without
      // the lock held and now; just recalculate it.
      remaining = mh->inUseCount();
      const bool shouldFlush = postFreeLocked(mh, sizeClass, remaining);
      if (unlikely(shouldFlush)) {
        flushBinLocked(sizeClass);
      }
    } else {
      shouldMesh = !isAttached;
    }
  }

  if (shouldMesh) {
    maybeMesh();
  }
}

int GlobalHeap::mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  unique_lock<mutex> lock(_miniheapLock);

  if (!oldp || !oldlenp || *oldlenp < sizeof(size_t))
    return -1;

  auto statp = reinterpret_cast<size_t *>(oldp);

  if (strcmp(name, "mesh.check_period") == 0) {
    *statp = _meshPeriod;
    if (!newp || newlen < sizeof(size_t))
      return -1;
    auto newVal = reinterpret_cast<size_t *>(newp);
    _meshPeriod = *newVal;
    // resetNextMeshCheck();
  } else if (strcmp(name, "mesh.scavenge") == 0) {
    lock.unlock();
    scavenge(true);
    lock.lock();
  } else if (strcmp(name, "mesh.compact") == 0) {
    meshAllSizeClassesLocked();
    lock.unlock();
    scavenge(true);
    lock.lock();
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

void GlobalHeap::meshLocked(MiniHeap *dst, MiniHeap *&src) {
  // mesh::debug("mesh dst:%p <- src:%p\n", dst, src);
  // dst->dumpDebug();
  // src->dumpDebug();
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
  postFreeLocked(dst, dst->sizeClass(), dst->inUseCount());
  untrackMiniheapLocked(src);
}

size_t GlobalHeap::meshSizeClassLocked(size_t sizeClass, MergeSetArray &mergeSets, SplitArray &left,
                                       SplitArray &right) {
  size_t mergeSetCount = 0;
  // memset(reinterpret_cast<void *>(&mergeSets), 0, sizeof(mergeSets));
  // memset(&left, 0, sizeof(left));
  // memset(&right, 0, sizeof(right));

  auto meshFound =
      function<bool(std::pair<MiniHeap *, MiniHeap *> &&)>([&](std::pair<MiniHeap *, MiniHeap *> &&miniheaps) {
        if (miniheaps.first->isMeshingCandidate() && miniheaps.second->isMeshingCandidate()) {
          mergeSets[mergeSetCount] = std::move(miniheaps);
          mergeSetCount++;
        }
        return mergeSetCount < kMaxMergeSets;
      });

  method::shiftedSplitting(_fastPrng, &_partialFreelist[sizeClass].first, left, right, meshFound);

  if (mergeSetCount == 0) {
    // debug("nothing to mesh.");
    return 0;
  }

  size_t meshCount = 0;

  for (size_t i = 0; i < mergeSetCount; i++) {
    std::pair<MiniHeap *, MiniHeap *> &mergeSet = mergeSets[i];
    MiniHeap *dst = mergeSet.first;
    MiniHeap *src = mergeSet.second;
    d_assert(dst != nullptr);
    d_assert(src != nullptr);

    // merge _into_ the one with a larger mesh count, potentially
    // swapping the order of the pair
    const auto dstCount = src->meshCount();
    const auto srcCount = src->meshCount();
    if (srcCount + srcCount > kMaxMeshes) {
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

    if (!oneEmpty && !aboveMeshThreshold()) {
      meshLocked(dst, src);
      meshCount++;
    }
  }

  // flush things once more (since we may have called postFree instead
  // of mesh above)
  flushBinLocked(sizeClass);

  return meshCount;
}

void GlobalHeap::meshAllSizeClassesLocked() {
  static MergeSetArray PAGE_ALIGNED MergeSets;
  static_assert(sizeof(MergeSets) == sizeof(void *) * 2 * 4096, "array too big");
  d_assert((reinterpret_cast<uintptr_t>(&MergeSets) & (kPageSize - 1)) == 0);

  static SplitArray PAGE_ALIGNED Left;
  static SplitArray PAGE_ALIGNED Right;
  static_assert(sizeof(Left) == sizeof(void *) * 16384, "array too big");
  static_assert(sizeof(Right) == sizeof(void *) * 16384, "array too big");
  d_assert((reinterpret_cast<uintptr_t>(&Left) & (kPageSize - 1)) == 0);
  d_assert((reinterpret_cast<uintptr_t>(&Right) & (kPageSize - 1)) == 0);

  // if we have freed but not reset meshed mappings, this will reset
  // them to the identity mapping, ensuring we don't blow past our VMA
  // limit (which is why we set the force flag to true)
  Super::scavenge(true);

  if (!_lastMeshEffective.load(std::memory_order::memory_order_acquire)) {
    return;
  }

  if (Super::aboveMeshThreshold()) {
    return;
  }

  lock_guard<EpochLock> epochLock(_meshEpoch);

  // const auto start = time::now();

  // first, clear out any free memory we might have
  for (size_t sizeClass = 0; sizeClass < kNumBins; sizeClass++) {
    flushBinLocked(sizeClass);
  }

  size_t totalMeshCount = 0;

  for (size_t sizeClass = 0; sizeClass < kNumBins; sizeClass++) {
    totalMeshCount += meshSizeClassLocked(sizeClass, MergeSets, Left, Right);
  }

  madvise(&Left, sizeof(Left), MADV_DONTNEED);
  madvise(&Right, sizeof(Right), MADV_DONTNEED);
  madvise(&MergeSets, sizeof(MergeSets), MADV_DONTNEED);

  _lastMeshEffective = totalMeshCount > 256;
  _stats.meshCount += totalMeshCount;

  Super::scavenge(true);

  _lastMesh = time::now();

  // const std::chrono::duration<double> duration = _lastMesh - start;
  // debug("mesh took %f, found %zu", duration.count(), totalMeshCount);
}

void GlobalHeap::dumpStats(int level, bool beDetailed) const {
  if (level < 1)
    return;

  lock_guard<mutex> lock(_miniheapLock);

  const auto meshedPageHWM = meshedPageHighWaterMark();

  debug("MESH COUNT:         %zu\n", (size_t)_stats.meshCount);
  debug("Meshed MB (total):  %.1f\n", (size_t)_stats.meshCount * 4096.0 / 1024.0 / 1024.0);
  debug("Meshed pages HWM:   %zu\n", meshedPageHWM);
  debug("Meshed MB HWM:      %.1f\n", meshedPageHWM * 4096.0 / 1024.0 / 1024.0);
  // debug("Peak RSS reduction: %.2f\n", rssSavings);
  debug("MH Alloc Count:     %zu\n", (size_t)_stats.mhAllocCount);
  debug("MH Free  Count:     %zu\n", (size_t)_stats.mhFreeCount);
  debug("MH High Water Mark: %zu\n", (size_t)_stats.mhHighWaterMark);
  if (level > 1) {
    // for (size_t i = 0; i < kNumBins; i++) {
    //   _littleheaps[i].dumpStats(beDetailed);
    // }
  }
}

namespace method {

void ATTRIBUTE_NEVER_INLINE halfSplit(MWC &prng, MiniHeapListEntry *miniheaps, SplitArray &left, size_t &leftSize,
                                      SplitArray &right, size_t &rightSize) noexcept {
  d_assert(leftSize == 0);
  d_assert(rightSize == 0);
  MiniHeapID mhId = miniheaps->next();
  while (mhId != list::Head && leftSize < kMaxSplitListSize && rightSize < kMaxSplitListSize) {
    auto mh = GetMiniHeap(mhId);
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

void ATTRIBUTE_NEVER_INLINE
shiftedSplitting(MWC &prng, MiniHeapListEntry *miniheaps, SplitArray &left, SplitArray &right,
                 const function<bool(std::pair<MiniHeap *, MiniHeap *> &&)> &meshFound) noexcept {
  constexpr size_t t = 64;

  if (miniheaps->empty()) {
    return;
  }

  size_t leftSize = 0;
  size_t rightSize = 0;

  halfSplit(prng, miniheaps, left, leftSize, right, rightSize);

  if (leftSize == 0 || rightSize == 0) {
    return;
  }

  constexpr size_t nBytes = 32;
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
        std::pair<MiniHeap *, MiniHeap *> heaps{h1, h2};
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
