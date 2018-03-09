// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdlib.h>

#include "runtime.h"
#include "thread_local_heap.h"

using namespace mesh;

static __attribute__((constructor)) void libmesh_init() {
  mesh::real::init();

  runtime().createSignalFd();
  runtime().installSegfaultHandler();

  char *meshPeriodStr = getenv("MESH_PERIOD_SECS");
  if (meshPeriodStr) {
    double period = strtod(meshPeriodStr, nullptr);
    runtime().setMeshPeriodSecs(period);
  }

  char *bgThread = getenv("MESH_BACKGROUND_THREAD");
  if (!bgThread)
    return;

  int shouldThread = atoi(bgThread);
  if (shouldThread)
    runtime().startBgThread();
}

static __attribute__((destructor)) void libmesh_fini() {
  char *mstats = getenv("MALLOCSTATS");
  if (!mstats)
    return;

  int mlevel = atoi(mstats);
  if (mlevel < 0)
    mlevel = 0;
  else if (mlevel > 2)
    mlevel = 2;

  runtime().heap().dumpStats(mlevel, false);
}

namespace mesh {
ATTRIBUTE_NEVER_INLINE
static void *allocSlowpath(size_t sz) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetHeap();
  return localHeap->malloc(sz);
}

ATTRIBUTE_NEVER_INLINE
static void freeSlowpath(void *ptr) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetHeap();
  localHeap->free(ptr);
}

ATTRIBUTE_NEVER_INLINE
static size_t usableSizeSlowpath(void *ptr) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetHeap();
  return localHeap->getSize(ptr);
}
}  // namespace mesh

extern "C" CACHELINE_ALIGNED_FN void *mesh_malloc(size_t sz) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetFastPathHeap();

  if (unlikely(localHeap == nullptr)) {
    return mesh::allocSlowpath(sz);
  }

  return localHeap->malloc(sz);
}
#define xxmalloc mesh_malloc

extern "C" CACHELINE_ALIGNED_FN void mesh_free(void *ptr) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetFastPathHeap();

  if (unlikely(localHeap == nullptr)) {
    mesh::freeSlowpath(ptr);
    return;
  }

  return localHeap->free(ptr);
}
#define xxfree mesh_free

extern "C" CACHELINE_ALIGNED_FN size_t mesh_malloc_usable_size(void *ptr) {
  ThreadLocalHeap *localHeap = ThreadLocalHeap::GetFastPathHeap();

  if (unlikely(localHeap == nullptr)) {
    return mesh::usableSizeSlowpath(ptr);
  }

  return localHeap->getSize(ptr);
}
#define xxmalloc_usable_size mesh_malloc_usable_size

extern "C" {
size_t mesh_usable_size(void *ptr) {
  return xxmalloc_usable_size(ptr);
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking.  This is not normally invoked when
// libmesh is dynamically linked or LD_PRELOADed into a binary.
void xxmalloc_lock(void) {
  mesh::runtime().lock();
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking.  This is not normally invoked when
// libmesh is dynamically linked or LD_PRELOADed into a binary.
void xxmalloc_unlock(void) {
  mesh::runtime().unlock();
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  return mesh::runtime().sigaction(signum, act, oldact);
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  return mesh::runtime().sigprocmask(how, set, oldset);
}

// we need to wrap pthread_create so that we can safely implement a
// stop-the-world quiescent period for the copy/mremap phase of
// meshing
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, mesh::PthreadFn startRoutine, void *arg) {
  return mesh::runtime().createThread(thread, attr, startRoutine, arg);
}

// Same API as je_mallctl, allows a program to query stats and set
// allocator-related options.
int mesh_mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  return mesh::runtime().heap().mallctl(name, oldp, oldlenp, newp, newlen);
}

int epoll_wait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout) {
  return mesh::runtime().epollWait(__epfd, __events, __maxevents, __timeout);
}

int epoll_pwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout, const __sigset_t *__ss) {
  return mesh::runtime().epollPwait(__epfd, __events, __maxevents, __timeout, __ss);
}

int mesh_in_bounds(void *ptr) {
  return mesh::runtime().heap().inBounds(ptr);
}

int mesh_bit_get(enum mesh::BitType type, void *ptr) {
  return mesh::runtime().heap().bitmapGet(type, ptr);
}

int mesh_bit_set(enum mesh::BitType type, void *ptr) {
  return mesh::runtime().heap().bitmapSet(type, ptr);
}

int mesh_bit_clear(enum mesh::BitType type, void *ptr) {
  return mesh::runtime().heap().bitmapClear(type, ptr);
}
}

#ifdef __linux__
#include "gnuwrapper.cpp"
#else
#include "macwrapper.cpp"
#endif
