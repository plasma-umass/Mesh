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

// we get here if the freelist is exhausted
void *ThreadLocalHeap::smallAllocSlowpath(size_t sizeClass) {
  Freelist &freelist = _freelist[sizeClass];

  // we are in the slowlist because we couldn't allocate out of this
  // freelist.  If there was an attached miniheap it is now full, so
  // detach it
  if (likely(freelist.isAttached())) {
    freelist.detach();
  }

  const size_t sizeMax = SizeMap::ByteSizeForClass(sizeClass);

  MiniHeap *mh = _global->allocSmallMiniheap(sizeClass, sizeMax);

  freelist.attach(_prng, mh);

  d_assert(freelist.isAttached());
  d_assert(!freelist.isExhausted());
  d_assert(mh->refcount() > 0);

  void *ptr = freelist.malloc();
  d_assert(ptr != nullptr);

  _last = &freelist;

  return ptr;
}

void ThreadLocalHeap::freeSlowpath(void *ptr) {
#if 1
  for (size_t i = 0; i < kNumBins; i++) {
    Freelist &freelist = _freelist[i];
    if (freelist.contains(ptr)) {
      freelist.free(ptr);
      _last = &freelist;
      return;
    }
  }
  _global->free(ptr);
#else
  // TODO: I like that this doesn't loop, but it causes us to crash
  // and I have no idea why.
  auto mh = _global->miniheapFor(ptr);
  if (likely(mh) && mh->maxCount() > 1) {
    const auto sizeClass = SizeMap::SizeClass(mh->objectSize());
    Freelist &freelist = _freelist[sizeClass];
    if (likely(freelist.getAttached() == mh)) {
      d_assert(mh->refcount() > 1);
      mh->unref();
      d_assert(mh->refcount() > 0);
      freelist.free(ptr);
      _last = &freelist;
      return;
    }
  }
  _global->freeFrom(mh, ptr);
#endif
}
}  // namespace mesh
