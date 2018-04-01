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

  // ensure we are asking for a multiple of the page size
  sz = RoundUpToPage(sz);

  // prevent integer underflows
  if (unlikely(sz > INT_MAX)) {
    return nullptr;
  }

  lock_guard<mutex> lock(_miniheapLock);

  MiniHeap *mh = allocMiniheapLocked(-1, PageCount(sz), 1, sz);

  d_assert(mh->maxCount() == 1);
  d_assert(mh->spanSize() == sz);
  d_assert(mh->objectSize() == sz);

  void *ptr = mh->mallocAt(0);

  mh->unref();

  return ptr;
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
