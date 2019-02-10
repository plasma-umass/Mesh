// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "thread_local_heap.h"

namespace mesh {

__thread ThreadLocalHeap::ThreadLocalData ThreadLocalHeap::_threadLocalData ATTR_INITIAL_EXEC CACHELINE_ALIGNED;

ThreadLocalHeap *ThreadLocalHeap::CreateThreadLocalHeap() {
  void *buf = mesh::internal::Heap().malloc(RoundUpToPage(sizeof(ThreadLocalHeap)));
  if (buf == nullptr) {
    mesh::debug("mesh: unable to allocate ThreadLocalHeap, aborting.\n");
    abort();
  }

  // hard_assert(reinterpret_cast<uintptr_t>(buf) % CACHELINE_SIZE == 0);

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
  shuffleVector.attach();

  d_assert(!shuffleVector.isExhausted());

  void *ptr = shuffleVector.malloc();
  d_assert(ptr != nullptr);

  return ptr;
}
}  // namespace mesh
