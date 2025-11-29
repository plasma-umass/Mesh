// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <stdlib.h>

#ifdef __linux__
#include <sys/auxv.h>
#include <unistd.h>
#endif

#include "runtime.h"
#include "thread_local_heap.h"
#include "runtime_impl.h"
#include "dispatch_utils.h"
#include "ifunc_resolver.h"

using namespace mesh;

static __attribute__((constructor)) void libmesh_init() {
  mesh::real::init();

  const size_t pageSize = getPageSize();

  // We ONLY support 4KB and 16KB page sizes. Fail immediately if unsupported.
  if (pageSize != kPageSize4K && pageSize != kPageSize16K) {
    mesh::debug("FATAL: Unsupported page size %zu bytes. Mesh only supports 4KB and 16KB pages.\n", pageSize);
    abort();
  }

  dispatchByPageSize([](auto &rt) {
    rt.createSignalFd();
    rt.installSegfaultHandler();
    rt.initMaxMapCount();
  });

  if (pageSize == kPageSize4K) {
    ThreadLocalHeap<kPageSize4K>::InitTLH();
  } else {
    ThreadLocalHeap<kPageSize16K>::InitTLH();
  }

  char *meshPeriodStr = getenv("MESH_PERIOD_MS");
  if (meshPeriodStr) {
    long period = strtol(meshPeriodStr, nullptr, 10);
    if (period < 0) {
      period = 0;
    }
    dispatchByPageSize([period](auto &rt) { rt.setMeshPeriodMs(std::chrono::milliseconds{period}); });
  }

  char *bgThread = getenv("MESH_BACKGROUND_THREAD");
  if (!bgThread)
    return;

  int shouldThread = atoi(bgThread);
  if (shouldThread) {
    dispatchByPageSize([](auto &rt) { rt.startBgThread(); });
  }
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

  dispatchByPageSize([mlevel](auto &rt) { rt.heap().dumpStats(mlevel, false); });
}

namespace mesh {

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE void *allocSlowpath(size_t sz) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->malloc(sz);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE __attribute__((unused)) void *cxxNewSlowpath(size_t sz) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->cxxNew(sz);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE void freeSlowpath(void *ptr) {
  // instead of instantiating a thread-local heap on free, just free
  // to the global heap directly
  runtime<PageSize>().heap().free(ptr);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE void *reallocSlowpath(void *oldPtr, size_t newSize) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->realloc(oldPtr, newSize);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE void *callocSlowpath(size_t count, size_t size) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->calloc(count, size);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE size_t usableSizeSlowpath(void *ptr) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->getSize(ptr);
}

template <size_t PageSize>
ATTRIBUTE_NEVER_INLINE void *memalignSlowpath(size_t alignment, size_t size) {
  ThreadLocalHeap<PageSize> *localHeap = ThreadLocalHeap<PageSize>::GetHeap();
  return localHeap->memalign(alignment, size);
}
}  // namespace mesh

// Implementation templates for IFUNC or dispatch
template <size_t PageSize>
static void *mesh_malloc_impl(size_t sz) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::allocSlowpath<PageSize>(sz);
  }
  return localHeap->malloc(sz);
}

template <size_t PageSize>
static void mesh_free_impl(void *ptr) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    mesh::freeSlowpath<PageSize>(ptr);
    return;
  }
  localHeap->free(ptr);
}

template <size_t PageSize>
static void mesh_sized_free_impl(void *ptr, size_t sz) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    mesh::freeSlowpath<PageSize>(ptr);
    return;
  }
  localHeap->sizedFree(ptr, sz);
}

template <size_t PageSize>
static void *mesh_realloc_impl(void *oldPtr, size_t newSize) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::reallocSlowpath<PageSize>(oldPtr, newSize);
  }
  return localHeap->realloc(oldPtr, newSize);
}

template <size_t PageSize>
static size_t mesh_malloc_usable_size_impl(void *ptr) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::usableSizeSlowpath<PageSize>(ptr);
  }
  return localHeap->getSize(ptr);
}

template <size_t PageSize>
static void *mesh_memalign_impl(size_t alignment, size_t size) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::memalignSlowpath<PageSize>(alignment, size);
  }
  return localHeap->memalign(alignment, size);
}

template <size_t PageSize>
static void *mesh_calloc_impl(size_t count, size_t size) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::callocSlowpath<PageSize>(count, size);
  }
  return localHeap->calloc(count, size);
}

#if defined(__linux__) && defined(__aarch64__)
// ===================================================================
// IFUNC Resolver Functions (ARM64 Linux only)
// ===================================================================
// These resolver functions are called by the dynamic linker to determine
// which implementation to use for each memory allocation function.
// They run ONCE per function at program startup, before main().
//
// The resolver selects between 4KB and 16KB page implementations based
// on the actual system page size detected from the auxiliary vector.
//
// CRITICAL: These functions run in the restricted IFUNC environment:
//   - No access to global variables (not initialized yet)
//   - No library functions available
//   - Must be completely self-contained
//   - Must use no_stack_protector attribute (stack guard not set up)
//
// The dynamic linker replaces calls to mesh_malloc, mesh_free, etc.
// with direct calls to the selected implementation (mesh_malloc_impl<4096>
// or mesh_malloc_impl<16384>), eliminating runtime overhead.
//
// Note: x86_64 Linux always uses 4KB pages, so we use compile-time
// dispatch instead of IFUNC there (the branch is optimized away).
// ===================================================================
extern "C" {
typedef void *(*malloc_func)(size_t);
typedef void (*free_func)(void *);
typedef void (*sized_free_func)(void *, size_t);
typedef void *(*realloc_func)(void *, size_t);
typedef size_t (*usable_size_func)(void *);
typedef void *(*memalign_func)(size_t, size_t);
typedef void *(*calloc_func)(size_t, size_t);

__attribute__((no_stack_protector)) static malloc_func resolve_mesh_malloc() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_malloc_impl<kPageSize4K> : mesh_malloc_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static free_func resolve_mesh_free() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_free_impl<kPageSize4K> : mesh_free_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static sized_free_func resolve_mesh_sized_free() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_sized_free_impl<kPageSize4K> : mesh_sized_free_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static realloc_func resolve_mesh_realloc() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_realloc_impl<kPageSize4K> : mesh_realloc_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static usable_size_func resolve_mesh_malloc_usable_size() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_malloc_usable_size_impl<kPageSize4K>
                                   : mesh_malloc_usable_size_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static memalign_func resolve_mesh_memalign() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_memalign_impl<kPageSize4K> : mesh_memalign_impl<kPageSize16K>;
}
__attribute__((no_stack_protector)) static calloc_func resolve_mesh_calloc() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? mesh_calloc_impl<kPageSize4K> : mesh_calloc_impl<kPageSize16K>;
}
}
#endif

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void *mesh_malloc(size_t sz)
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_malloc")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return mesh_malloc_impl<kPageSize4K>(sz);
  } else {
    return mesh_malloc_impl<kPageSize16K>(sz);
  }
}
#endif
#define xxmalloc mesh_malloc

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void mesh_free(void *ptr)
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_free")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    mesh_free_impl<kPageSize4K>(ptr);
  } else {
    mesh_free_impl<kPageSize16K>(ptr);
  }
}
#endif
#define xxfree mesh_free

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void mesh_sized_free(void *ptr, size_t sz)
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_sized_free")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    mesh_sized_free_impl<kPageSize4K>(ptr, sz);
  } else {
    mesh_sized_free_impl<kPageSize16K>(ptr, sz);
  }
}
#endif

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void *mesh_realloc(void *oldPtr, size_t newSize)
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_realloc")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return mesh_realloc_impl<kPageSize4K>(oldPtr, newSize);
  } else {
    return mesh_realloc_impl<kPageSize16K>(oldPtr, newSize);
  }
}
#endif

#if defined(__FreeBSD__)
extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void *mesh_reallocarray(void *oldPtr, size_t count, size_t size) {
  size_t total;
  if (unlikely(__builtin_umull_overflow(count, size, &total))) {
    return NULL;
  } else {
    return mesh_realloc(oldPtr, total);
  }
}
#endif

#ifndef __FreeBSD__
extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN size_t mesh_malloc_usable_size(void *ptr)
#else
extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN size_t mesh_malloc_usable_size(const void *cptr)
#endif
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_malloc_usable_size")));
#else
{
#ifdef __FreeBSD__
  void *ptr = const_cast<void *>(cptr);
#endif
  if (likely(getPageSize() == kPageSize4K)) {
    return mesh_malloc_usable_size_impl<kPageSize4K>(ptr);
  } else {
    return mesh_malloc_usable_size_impl<kPageSize16K>(ptr);
  }
}
#endif
#define xxmalloc_usable_size mesh_malloc_usable_size

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void *mesh_memalign(size_t alignment, size_t size)
#if !defined(__FreeBSD__) && !defined(__SVR4)
    throw()
#endif
#if defined(__linux__) && defined(__aarch64__)
        __attribute__((ifunc("resolve_mesh_memalign")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return mesh_memalign_impl<kPageSize4K>(alignment, size);
  } else {
    return mesh_memalign_impl<kPageSize16K>(alignment, size);
  }
}
#endif

extern "C" MESH_EXPORT CACHELINE_ALIGNED_FN void *mesh_calloc(size_t count, size_t size)
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_mesh_calloc")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return mesh_calloc_impl<kPageSize4K>(count, size);
  } else {
    return mesh_calloc_impl<kPageSize16K>(count, size);
  }
}
#endif

extern "C" {
#ifdef __linux__
size_t MESH_EXPORT mesh_usable_size(void *ptr) __attribute__((weak, alias("mesh_malloc_usable_size")));
#else
// aliases are not supported on darwin
size_t MESH_EXPORT mesh_usable_size(void *ptr) {
  return mesh_malloc_usable_size(ptr);
}
#endif  // __linux__

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking.  This is not normally invoked when
// libmesh is dynamically linked or LD_PRELOADed into a binary.
void MESH_EXPORT xxmalloc_lock(void) {
  mesh::dispatchByPageSize([](auto &rt) { rt.lock(); });
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking.  This is not normally invoked when
// libmesh is dynamically linked or LD_PRELOADed into a binary.
void MESH_EXPORT xxmalloc_unlock(void) {
  mesh::dispatchByPageSize([](auto &rt) { rt.unlock(); });
}

int MESH_EXPORT sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) MESH_THROW {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.sigaction(signum, act, oldact); });
}

int MESH_EXPORT sigprocmask(int how, const sigset_t *set, sigset_t *oldset) MESH_THROW {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.sigprocmask(how, set, oldset); });
}

// we need to wrap pthread_create and pthread_exit so that we can
// install our segfault handler and cleanup thread-local heaps.
int MESH_EXPORT pthread_create(pthread_t *thread, const pthread_attr_t *attr, mesh::PthreadFn startRoutine,
                               void *arg) MESH_THROW {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.createThread(thread, attr, startRoutine, arg); });
}

void MESH_EXPORT ATTRIBUTE_NORETURN pthread_exit(void *retval) {
  if (likely(getPageSize() == kPageSize4K)) {
    mesh::runtime<kPageSize4K>().exitThread(retval);
  } else {
    mesh::runtime<kPageSize16K>().exitThread(retval);
  }
}

// Same API as je_mallctl, allows a program to query stats and set
// allocator-related options.
int MESH_EXPORT mesh_mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.heap().mallctl(name, oldp, oldlenp, newp, newlen); });
}

#ifdef __linux__

int MESH_EXPORT epoll_wait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout) {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.epollWait(__epfd, __events, __maxevents, __timeout); });
}

int MESH_EXPORT epoll_pwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout,
                            const __sigset_t *__ss) {
  return mesh::dispatchByPageSize(
      [=](auto &rt) { return rt.epollPwait(__epfd, __events, __maxevents, __timeout, __ss); });
}

#endif
#if __linux__

ssize_t MESH_EXPORT recv(int sockfd, void *buf, size_t len, int flags) {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.recv(sockfd, buf, len, flags); });
}

ssize_t MESH_EXPORT recvmsg(int sockfd, struct msghdr *msg, int flags) {
  return mesh::dispatchByPageSize([=](auto &rt) { return rt.recvmsg(sockfd, msg, flags); });
}
#endif
}

#if defined(__linux__)
#include "gnu_wrapper.cc"
#elif defined(__APPLE__)
#include "mac_wrapper.cc"
#elif defined(__FreeBSD__)
#include "fbsd_wrapper.cc"
#else
#error "only linux, macOS and FreeBSD support for now"
#endif