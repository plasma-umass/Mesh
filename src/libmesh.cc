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

// This is required to properly use internal_sigaction.
namespace __sanitizer {
int real_sigaction(int signum, const void *act, void *oldact) {
  mesh::debug("TODO: implement real_sigaction\n");
  _exit(1);
  // if (REAL(sigaction) == nullptr) {
  //   // With an instrumented allocator, this is called during interceptor init
  //   // and we need a raw syscall solution.
  //   return internal_sigaction_syscall(signum, act, oldact);
  // }
  // return REAL(sigaction)(signum, (const struct sigaction *)act,
  //                        (struct sigaction *)oldact);
}
}  // namespace __sanitizer

namespace __sanitizer {
int real_pthread_create(void *th, void *attr, void *(*callback)(void *), void *param) {
  mesh::debug("TODO: implement real_pthread_create\n");
  _exit(1);
}
int real_pthread_join(void *th, void **ret) {
  mesh::debug("TODO: implement real_pthread_join\n");
  _exit(1);
}
}  // namespace __sanitizer

extern "C" {
void __sanitizer_print_memory_profile(size_t top_percent, size_t max_number_of_contexts) {
  mesh::debug("TODO: __sanitizer_print_memory_profile\n");
  _exit(1);
}

bool __sanitizer_symbolize_code(const char *ModuleName, uint64_t ModuleOffset, char *Buffer, int MaxLength) {
  mesh::debug("TODO: __sanitizer_symbolize_code\n");
  _exit(1);
}

bool __sanitizer_symbolize_data(const char *ModuleName, uint64_t ModuleOffset, char *Buffer, int MaxLength) {
  mesh::debug("TODO: __sanitizer_symbolize_data\n");
  _exit(1);
}

void __sanitizer_symbolize_flush() {
  mesh::debug("TODO: __sanitizer_symbolize_flush\n");
  _exit(1);
}

int __sanitizer_symbolize_demangle(const char *Name, char *Buffer, int MaxLength) {
  mesh::debug("TODO: __sanitizer_symbolize_demangle\n");
  _exit(1);
}

int real_pthread_attr_getstack(void *attr, void **addr, size_t *size) {
  mesh::debug("TODO: real_pthread_attr_getstack\n");
  _exit(1);
}

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
