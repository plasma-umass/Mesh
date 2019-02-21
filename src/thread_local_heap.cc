// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include "thread_local_heap.h"

namespace mesh {

static __thread char threadLocalBuffer[RoundUpToPage(sizeof(ThreadLocalHeap))] CACHELINE_ALIGNED ATTR_INITIAL_EXEC;
__thread ThreadLocalHeap::ThreadLocalData ThreadLocalHeap::_threadLocalData ATTR_INITIAL_EXEC CACHELINE_ALIGNED;

ThreadLocalHeap *ThreadLocalHeap::CreateThreadLocalHeap() {
  void *buf = &threadLocalBuffer;
  hard_assert(buf != nullptr);
  hard_assert(reinterpret_cast<uintptr_t>(buf) % CACHELINE_SIZE == 0);

  return new (buf) ThreadLocalHeap(&mesh::runtime().heap());
}

void ThreadLocalHeap::releaseAll() {
  for (size_t i = 1; i < kNumBins; i++) {
    _shuffleVector[i].refillMiniheaps();
    _global->releaseMiniheaps(_shuffleVector[i].miniheaps());
  }
}

ThreadLocalHeap *ThreadLocalHeap::GetHeap() {
  auto heap = GetFastPathHeap();
  if (heap == nullptr) {
    heap = CreateThreadLocalHeap();
    _threadLocalData.fastpathHeap = heap;
  }
  return heap;
}

// we get here if the shuffleVector is exhausted
void *CACHELINE_ALIGNED_FN ThreadLocalHeap::smallAllocSlowpath(size_t sizeClass) {
  ShuffleVector &shuffleVector = _shuffleVector[sizeClass];

  // we grab multiple MiniHeaps at a time from the global heap.  often
  // it is possible to refill the freelist from a not-yet-used
  // MiniHeap we already have, without global cross-thread
  // synchronization
  if (likely(shuffleVector.localRefill())) {
    return shuffleVector.malloc();
  }

  return smallAllocGlobalRefill(shuffleVector, sizeClass);
}

void *CACHELINE_ALIGNED_FN ThreadLocalHeap::smallAllocGlobalRefill(ShuffleVector &shuffleVector, size_t sizeClass) {
  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  _global->allocSmallMiniheaps(sizeClass, sizeMax, shuffleVector.miniheaps(), _current);
  shuffleVector.reinit();

  d_assert(!shuffleVector.isExhausted());

  void *ptr = shuffleVector.malloc();
  d_assert(ptr != nullptr);

  return ptr;
}
}  // namespace mesh
