// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "runtime.h"

#include "wrappers/gnuwrapper.cpp"

static __attribute__((constructor)) void libmesh_init() {
  // force the runtime's constructor to run
  volatile auto mem = mesh::runtime().heap().malloc(0);
  if (mem != nullptr)
    mesh::runtime().heap().free(mem);
}

extern "C" {
void *xxmalloc(size_t sz) {
  return mesh::runtime().heap().malloc(sz);
}

void xxfree(void *ptr) {
  mesh::runtime().heap().free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return mesh::runtime().heap().getSize(ptr);
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
}
