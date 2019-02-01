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
    auto mh = _shuffleVector[i].refillAndDetach();
    _global->releaseMiniheap(mh);
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
void *ThreadLocalHeap::smallAllocSlowpath(size_t sizeClass) {
  ShuffleVector &shuffleVector = _shuffleVector[sizeClass];

  MiniHeap *oldMH = nullptr;
  // we are in the slowlist because we couldn't allocate out of this
  // shuffleVector.  If there was an attached miniheap it is now full, so
  // detach it
  if (likely(shuffleVector.isAttached())) {
    oldMH = shuffleVector.detach();
  }

  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  MiniHeap *mh = _global->allocSmallMiniheap(sizeClass, sizeMax, oldMH, _current);
  d_assert(mh != nullptr);

  shuffleVector.attach(_global->arenaBegin(), mh);

  d_assert(shuffleVector.isAttached());
  d_assert(!shuffleVector.isExhausted());
  d_assert(mh->isAttached());

  void *ptr = shuffleVector.malloc();
  d_assert(ptr != nullptr);
  _last = &shuffleVector;

  return ptr;
}
}  // namespace mesh
