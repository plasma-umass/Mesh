// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "thread_local_heap.h"

namespace mesh {

__thread ThreadLocalHeap::ThreadLocalData ThreadLocalHeap::_threadLocalData ATTR_INITIAL_EXEC CACHELINE_ALIGNED;

ThreadLocalHeap *ThreadLocalHeap::CreateThreadLocalHeap() {
  void *buf = mesh::internal::Heap().malloc(sizeof(ThreadLocalHeap) + 2 * CACHELINE_SIZE);
  if (buf == nullptr) {
    mesh::debug("mesh: unable to allocate ThreadLocalHeap, aborting.\n");
    abort();
  }

  void *alignedPtr = (void *)(((uintptr_t)buf + CACHELINE_SIZE - 1) & ~(CACHELINE_SIZE - 1));

  return new (alignedPtr) ThreadLocalHeap(&mesh::runtime().heap());
}

ThreadLocalHeap *ThreadLocalHeap::GetHeap() {
  auto heap = GetFastPathHeap();
  if (heap == nullptr) {
    heap = CreateThreadLocalHeap();
    _threadLocalData.fastpathHeap = heap;
  }
  return heap;
}

void ThreadLocalHeap::attachFreelist(Freelist &freelist, size_t sizeClass) {
  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  MiniHeap *mh = _global->allocMiniheap(sizeMax);

  const auto allocCount = freelist.attach(_prng, mh);
  d_assert(freelist.isAttached());
  d_assert(!freelist.isExhausted());

  // tell the miniheap how many offsets we pulled out/preallocated into our freelist
  mh->reattach(allocCount);

  d_assert(mh->isAttached());
}

// we get here if the freelist is exhausted
void *ThreadLocalHeap::mallocSlowpath(size_t sizeClass, size_t sz) {
  Freelist &freelist = _freelist[sizeClass];

  if (&freelist == _last) {
    _last = nullptr;
  }
  if (freelist.isAttached()) {
    freelist.detach();
  }

  attachFreelist(freelist, sizeClass);

  void *ptr = freelist.malloc();
  _last = &freelist;

  return ptr;
}

void ThreadLocalHeap::freeSlowpath(void *ptr) {
  for (size_t i = 0; i < kNumBins; i++) {
    Freelist &freelist = _freelist[i];
    if (freelist.contains(ptr)) {
      freelist.free(ptr);
      _last = &freelist;
      return;
    }
  }

  _global->free(ptr);
}
}  // namespace mesh
