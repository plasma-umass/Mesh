// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "global_heap.h"

namespace mesh {

void *GlobalHeap::malloc(size_t sz) {
#ifndef NDEBUG
  if (unlikely(sz <= kMaxSize)) {
    abort();
  }
#endif

  const auto pageCount = PageCount(sz);

  // prevent integer underflows
  if (unlikely(pageCount * kPageSize > INT_MAX)) {
    return nullptr;
  }

  return pageAlignedAlloc(1, pageCount);
}

void GlobalHeap::free(void *ptr) {
  lock_guard<mutex> lock(_miniheapLock);

  auto mh = miniheapForLocked(ptr);
  if (unlikely(!mh)) {
    // FIXME: we should warn/error or something here after we add an
    // aligned allocate API
    return;
  }

  // large objects are tracked with a miniheap per object and don't
  // trigger meshing, because they are multiples of the page size.
  // This can also include, for example, single page allocations w/
  // 16KB alignment.
  if (mh->maxCount() == 1) {
    // we need to grab the exclusive lock here, as the read-only
    // lock we took in miniheapFor has already been released
    freeMiniheapLocked(mh, false);
    return;
  }

  d_assert(mh->maxCount() > 1);

  _lastMeshEffective = 1;
  mh->free(ptr);

  bool shouldConsiderMesh = !mh->isEmpty();

  const auto sizeClass = SizeMap::SizeClass(mh->objectSize());

  // this may free the miniheap -- we can't safely access it after
  // this point.
  bool shouldFlush = _littleheaps[sizeClass].postFree(mh);
  mh = nullptr;

  if (unlikely(shouldFlush)) {
    flushBinLocked(sizeClass);
    Super::scavenge();
  }

  if (shouldConsiderMesh)
    maybeMesh();
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
  } else if (strcmp(name, "mesh.compact") == 0) {
    lock.unlock();
    meshAllSizeClasses();
    scavenge();
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
    for (size_t i = 0; i < kNumBins; i++) {
      const auto count = _littleheaps[i].nonEmptyCount();
      if (count == 0)
        continue;
      sz += count * _littleheaps[i].objectSize() * _littleheaps[i].objectCount();
    }
    *statp = sz;
  } else if (strcmp(name, "stats.allocated") == 0) {
    // TODO: revisit this
    // same as active for us, for now -- memory not returned to the OS
    size_t sz = 0;
    for (size_t i = 0; i < kNumBins; i++) {
      const auto &bin = _littleheaps[i];
      const auto count = bin.nonEmptyCount();
      if (count == 0)
        continue;
      sz += bin.objectSize() * bin.allocatedObjectCount();
    }
    *statp = sz;
  }
  return 0;
}
}  // namespace mesh
