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
    auto mh = _freelist[i].refillAndDetach();
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

// we get here if the freelist is exhausted
void *ThreadLocalHeap::smallAllocSlowpath(size_t sizeClass) {
  Freelist &freelist = _freelist[sizeClass];

  MiniHeap *oldMH = nullptr;
  // we are in the slowlist because we couldn't allocate out of this
  // freelist.  If there was an attached miniheap it is now full, so
  // detach it
  if (likely(freelist.isAttached())) {
    oldMH = freelist.detach();
  }

  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  MiniHeap *mh = _global->allocSmallMiniheap(sizeClass, sizeMax, oldMH);
  d_assert(mh != nullptr);

  freelist.attach(_global->arenaBegin(), mh);

  d_assert(freelist.isAttached());
  d_assert(!freelist.isExhausted());
  d_assert(mh->isAttached());

  void *ptr = freelist.malloc();
  d_assert(ptr != nullptr);
  _last = &freelist;

  return ptr;
}
}  // namespace mesh
