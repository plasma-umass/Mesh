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

  MiniHeap *mh = _global->allocSmallMiniheap(sizeMax);

  const auto allocCount = freelist.attach(_prng, mh);
  d_assert(freelist.isAttached());
  d_assert(!freelist.isExhausted());

  // tell the miniheap how many offsets we pulled out/preallocated into our freelist
  mh->reattach(allocCount);

  d_assert(mh->isAttached());

  mh->unref();
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
  // we used to loop over all attached miniheaps here. Instead, just
  // lookup the miniheap through the meshable-arena's metadata array.
  // We end up having to grab the mh RW lock read-only and frob the
  // miniheap's atomic reference, but we avoid a potentially poorly
  // predicted loop over all 25 attached feelists.  Performance
  // results seem to be a wash, and I like the simple non-loopy nature
  // of this approach.
  auto mh = _global->miniheapFor(ptr);
  if (likely(mh && mh->isOwnedBy(pthread_self()))) {
    const auto sizeClass = SizeMap::SizeClass(mh->objectSize());
    mh->unref();
    Freelist &freelist = _freelist[sizeClass];
    freelist.free(ptr);
    _last = &freelist;
    return;
  }

  _global->freeFrom(mh, ptr);
}
}  // namespace mesh
