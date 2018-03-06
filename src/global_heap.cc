// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "global_heap.h"

namespace mesh {
void *GlobalHeap::malloc(size_t sz) {
  // prevent integer underflows
  if (unlikely(sz > INT_MAX))
      return nullptr;

#ifndef NDEBUG
  if (unlikely(sz <= kMaxSize))
    abort();
#endif

  std::lock_guard<std::mutex> lock(_bigMutex);
  return _bigheap.malloc(sz);
}
}  // namespace mesh
