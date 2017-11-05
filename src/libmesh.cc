// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "runtime.h"

#include "wrappers/gnuwrapper.cpp"

using namespace mesh;

static __attribute__((constructor)) void libmesh_init() {
  // runtime().startBgThread();
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
}
