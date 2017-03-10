// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "runtime.h"

#include "wrappers/gnuwrapper.cpp"

using mesh::LocalHeap;

static __thread LocalHeap *localHeap;

static void allocLocalHeap() {
  d_assert(localHeap == nullptr);

  void *buf = mesh::internal::Heap().malloc(sizeof(LocalHeap));
  if (buf == nullptr) {
    mesh::debug("mesh: unable to allocate LocalHeap, aborting.\n");
    abort();
  }
  localHeap = new (buf) LocalHeap(&mesh::runtime().heap());
}

static __attribute__((constructor)) void libmesh_init() {
  if (localHeap == nullptr)
    allocLocalHeap();
}

extern "C" {
void *xxmalloc(size_t sz) {
  if (unlikely(localHeap == nullptr))
    allocLocalHeap();
  return localHeap->malloc(sz);
}

void xxfree(void *ptr) {
  if (unlikely(localHeap == nullptr))
    allocLocalHeap();
  localHeap->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  if (unlikely(localHeap == nullptr))
    allocLocalHeap();
  return localHeap->getSize(ptr);
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
