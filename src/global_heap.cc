// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "global_heap.h"

namespace mesh {

void *GlobalHeap::allocFromArena(size_t sz) {
  MiniHeap *mh = nullptr; // allocMiniheap(sz);

  d_assert(mh->maxCount() == 1);
  d_assert(mh->spanSize() == sz);
  d_assert(mh->objectSize() == sz);

  void *ptr = mh->mallocAtWithBitmap(0);

  return nullptr;
}

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

  // if (likely(sz <= kMaxFastLargeSize)) {
  //   return allocFromArena(sz);
  // }

  std::lock_guard<std::mutex> lock(_bigMutex);
  return _bigheap.malloc(sz);
}
}  // namespace mesh
