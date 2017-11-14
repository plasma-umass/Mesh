// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdlib.h>

#include "runtime.h"

#include "wrappers/gnuwrapper.cpp"

using namespace mesh;

static __attribute__((constructor)) void libmesh_init() {
  runtime().initInterposition();

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

extern "C" {
void *xxmalloc(size_t sz) {
  return runtime().localHeap()->malloc(sz);
}

void xxfree(void *ptr) {
  runtime().localHeap()->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return runtime().localHeap()->getSize(ptr);
}

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
}
