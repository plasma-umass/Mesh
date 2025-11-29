// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Heap-Layers and Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <stdint.h>
#include <stdlib.h>  // size_t
#include <string.h>  // for memcpy and memset

#include <new>

#include "common.h"
#include "thread_local_heap.h"
#include "dispatch_utils.h"
#include "ifunc_resolver.h"

using namespace mesh;

extern "C" {

void *xxmalloc(size_t);
void xxfree(void *);

// Takes a pointer and returns how much space it holds.
#ifndef __FreeBSD__
size_t xxmalloc_usable_size(void *);
#else
size_t xxmalloc_usable_size(const void *);
#endif

// Locks the heap(s), used prior to any invocation of fork().
void xxmalloc_lock();

// Unlocks the heap(s), after fork().
void xxmalloc_unlock();
}

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__FreeBSD__)
#include <stdlib.h>
#else
#include <malloc.h>  // for memalign
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Disable warnings about long (> 255 chars) identifiers.
#pragma warning(disable : 4786)
// Set inlining to the maximum possible depth.
#pragma inline_depth(255)
#pragma warning(disable : 4074)  // initializers put in compiler reserved area

#pragma comment(linker, "/disallowlib:libc.lib")
#pragma comment(linker, "/disallowlib:libcd.lib")
#pragma comment(linker, "/disallowlib:libcmt.lib")
#pragma comment(linker, "/disallowlib:libcmtd.lib")
#pragma comment(linker, "/disallowlib:msvcrtd.lib")

#else
#include <errno.h>
#endif

#ifndef CUSTOM_PREFIX
#define CUSTOM_PREFIX(x) x
#endif

#define CUSTOM_MALLOC(x) CUSTOM_PREFIX(malloc)(x)
#define CUSTOM_FREE(x) CUSTOM_PREFIX(free)(x)
#define CUSTOM_CFREE(x) CUSTOM_PREFIX(cfree)(x)
#define CUSTOM_REALLOC(x, y) CUSTOM_PREFIX(realloc)(x, y)
#define CUSTOM_CALLOC(x, y) CUSTOM_PREFIX(calloc)(x, y)
#define CUSTOM_MEMALIGN(x, y) CUSTOM_PREFIX(memalign)(x, y)
#define CUSTOM_POSIX_MEMALIGN(x, y, z) CUSTOM_PREFIX(posix_memalign)(x, y, z)
#define CUSTOM_ALIGNED_ALLOC(x, y) CUSTOM_PREFIX(aligned_alloc)(x, y)
#define CUSTOM_GETSIZE(x) CUSTOM_PREFIX(malloc_usable_size)(x)
#define CUSTOM_GOODSIZE(x) CUSTOM_PREFIX(malloc_good_size)(x)
#define CUSTOM_VALLOC(x) CUSTOM_PREFIX(valloc)(x)
#define CUSTOM_PVALLOC(x) CUSTOM_PREFIX(pvalloc)(x)
#define CUSTOM_RECALLOC(x, y, z) CUSTOM_PREFIX(recalloc)(x, y, z)
#define CUSTOM_STRNDUP(s, sz) CUSTOM_PREFIX(strndup)(s, sz)
#define CUSTOM_STRDUP(s) CUSTOM_PREFIX(strdup)(s)
#define CUSTOM_GETCWD(b, s) CUSTOM_PREFIX(getcwd)(b, s)
#define CUSTOM_GETENV(s) CUSTOM_PREFIX(getenv)(s)

// GNU-related routines:
#define CUSTOM_MALLOPT(x, y) CUSTOM_PREFIX(mallopt)(x, y)
#define CUSTOM_MALLOC_TRIM(s) CUSTOM_PREFIX(malloc_trim)(s)
#define CUSTOM_MALLOC_STATS(a) CUSTOM_PREFIX(malloc_stats)(a)
#define CUSTOM_MALLOC_GET_STATE(p) CUSTOM_PREFIX(malloc_get_state)(p)
#define CUSTOM_MALLOC_SET_STATE(p) CUSTOM_PREFIX(malloc_set_state)(p)
#define CUSTOM_MALLINFO(a) CUSTOM_PREFIX(mallinfo)(a)

#if defined(_WIN32)
#define MYCDECL __cdecl
#if !defined(NO_INLINE)
#define NO_INLINE __declspec(noinline)
#endif
#pragma inline_depth(255)

#if !defined(NDEBUG)
#define __forceinline inline
#endif

#else
#define MYCDECL
#endif

/***** generic malloc functions *****/

#include <stdio.h>

// extern "C" void *MYCDECL CUSTOM_MALLOC(size_t);

#if !defined(_WIN32)
extern "C" MESH_EXPORT int CUSTOM_POSIX_MEMALIGN(void **memptr, size_t alignment, size_t size)
#if !defined(__FreeBSD__) && !defined(__SVR4)
    throw()
#endif
{
  // Check for non power-of-two alignment.
  if ((alignment == 0) || (alignment & (alignment - 1))) {
    return EINVAL;
  }
  void *ptr = CUSTOM_MEMALIGN(alignment, size);
  if (!ptr) {
    return ENOMEM;
  } else {
    *memptr = ptr;
    return 0;
  }
}
#endif

extern "C" MESH_EXPORT void *MYCDECL CUSTOM_ALIGNED_ALLOC(size_t alignment, size_t size)
#if !defined(__FreeBSD__)
    throw()
#endif
{
  // Per the man page: "The function aligned_alloc() is the same as
  // memalign(), except for the added restriction that size should be
  // a multiple of alignment." Rather than check and potentially fail,
  // we just enforce this by rounding up the size, if necessary.
  size = size + alignment - (size % alignment);
  return CUSTOM_MEMALIGN(alignment, size);
}

extern "C" MESH_EXPORT void MYCDECL CUSTOM_CFREE(void *ptr) {
  xxfree(ptr);
}

extern "C" size_t MYCDECL CUSTOM_GOODSIZE(size_t sz) {
  void *ptr = xxmalloc(sz);
  size_t objSize = CUSTOM_GETSIZE(ptr);
  CUSTOM_FREE(ptr);
  return objSize;
}

#if defined(__linux)

extern "C" MESH_EXPORT char *MYCDECL CUSTOM_STRNDUP(const char *s, size_t sz) {
  char *newString = NULL;
  if (s != NULL) {
    size_t cappedLength = strnlen(s, sz);
    if ((newString = (char *)xxmalloc(cappedLength + 1))) {
      strncpy(newString, s, cappedLength);
      newString[cappedLength] = '\0';
    }
  }
  return newString;
}
#endif

extern "C" MESH_EXPORT char *MYCDECL CUSTOM_STRDUP(const char *src) {
  if (src == nullptr) {
    return nullptr;
  }

  // grab the string length once, use it for both malloc and strncpy calls
  const auto len = strlen(src) + 1;
  char *newString = reinterpret_cast<char *>(xxmalloc(len));
  if (newString != nullptr) {
    strncpy(newString, src, len);
  }

  return newString;
}

#if !defined(_WIN32)
#include <dlfcn.h>
#include <limits.h>

#if !defined(RTLD_NEXT)
#define RTLD_NEXT ((void *)-1)
#endif

typedef char *getcwdFunction(char *, size_t);

extern "C" MESH_EXPORT char *MYCDECL CUSTOM_GETCWD(char *buf, size_t size) {
  static getcwdFunction *real_getcwd =
      reinterpret_cast<getcwdFunction *>(reinterpret_cast<uintptr_t>(dlsym(RTLD_NEXT, "getcwd")));

  if (!buf) {
    if (size == 0) {
      size = PATH_MAX;
    }
    buf = (char *)xxmalloc(size);
  }
  return (real_getcwd)(buf, size);
}

#endif

extern "C" MESH_EXPORT int CUSTOM_MALLOPT(int /* param */, int /* value */) {
  // NOP.
  return 1;  // success.
}

extern "C" MESH_EXPORT int CUSTOM_MALLOC_TRIM(size_t /* pad */) {
  // NOP.
  return 0;  // no memory returned to OS.
}

extern "C" MESH_EXPORT void CUSTOM_MALLOC_STATS() {
  // NOP.
}

extern "C" MESH_EXPORT void *CUSTOM_MALLOC_GET_STATE() {
  return NULL;  // always returns "error".
}

extern "C" MESH_EXPORT int CUSTOM_MALLOC_SET_STATE(void * /* ptr */) {
  return 0;  // success.
}

#if defined(__GNUC__) && !defined(__FreeBSD__) && defined(__GLIBC__)
extern "C" MESH_EXPORT struct mallinfo CUSTOM_MALLINFO() {
  // For now, we return useless stats.
  struct mallinfo m;
  m.arena = 0;
  m.ordblks = 0;
  m.smblks = 0;
  m.hblks = 0;
  m.hblkhd = 0;
  m.usmblks = 0;
  m.fsmblks = 0;
  m.uordblks = 0;
  m.fordblks = 0;
  m.keepcost = 0;
  return m;
}
#endif

#if defined(__SVR4)
// Apparently we no longer need to replace new and friends for Solaris.
#define NEW_INCLUDED
#endif

#ifndef NEW_INCLUDED
#define NEW_INCLUDED

// ===================================================================
// C++ Operator Implementation Templates
// ===================================================================
// These template functions provide the actual implementation for each
// C++ memory operator. On ARM64 Linux, IFUNC resolvers select between
// 4KB and 16KB variants at load time. On x86_64 Linux, getPageSize()
// is constexpr (always 4KB), so the compiler optimizes away the branch.
// On other platforms, runtime dispatch is used.
// ===================================================================

template <size_t PageSize>
static void *cxx_new_impl(size_t sz) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::cxxNewSlowpath<PageSize>(sz);
  }
  return localHeap->cxxNew(sz);
}

template <size_t PageSize>
static void *cxx_new_nothrow_impl(size_t sz, const std::nothrow_t &) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    return mesh::allocSlowpath<PageSize>(sz);
  }
  return localHeap->malloc(sz);
}

template <size_t PageSize>
static void cxx_delete_impl(void *ptr) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    mesh::freeSlowpath<PageSize>(ptr);
    return;
  }
  localHeap->free(ptr);
}

template <size_t PageSize>
static void cxx_sized_delete_impl(void *ptr, size_t sz) {
  auto *localHeap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (unlikely(localHeap == nullptr)) {
    mesh::freeSlowpath<PageSize>(ptr);
    return;
  }
  localHeap->sizedFree(ptr, sz);
}

#if defined(__linux__) && defined(__aarch64__)
// ===================================================================
// IFUNC Resolvers for C++ Operators (ARM64 Linux only)
// ===================================================================
// These resolver functions run during dynamic linking to select the
// appropriate page-size-specific implementation. They use the IFUNC
// infrastructure from ifunc_resolver.h.
//
// Note: x86_64 Linux always uses 4KB pages, so we use compile-time
// dispatch instead of IFUNC there (the branch is optimized away).
// ===================================================================

extern "C" {
typedef void *(*cxx_new_func)(size_t);
typedef void *(*cxx_new_nothrow_func)(size_t, const std::nothrow_t &);
typedef void (*cxx_delete_func)(void *);
typedef void (*cxx_sized_delete_func)(void *, size_t);

__attribute__((no_stack_protector)) static cxx_new_func resolve_cxx_new() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? cxx_new_impl<kPageSize4K> : cxx_new_impl<kPageSize16K>;
}

__attribute__((no_stack_protector)) static cxx_new_nothrow_func resolve_cxx_new_nothrow() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? cxx_new_nothrow_impl<kPageSize4K> : cxx_new_nothrow_impl<kPageSize16K>;
}

__attribute__((no_stack_protector)) static cxx_delete_func resolve_cxx_delete() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? cxx_delete_impl<kPageSize4K> : cxx_delete_impl<kPageSize16K>;
}

__attribute__((no_stack_protector)) static cxx_sized_delete_func resolve_cxx_sized_delete() {
  size_t pageSize = mesh::ifunc::getPageSizeFromAuxv();
  return (pageSize == kPageSize4K) ? cxx_sized_delete_impl<kPageSize4K> : cxx_sized_delete_impl<kPageSize16K>;
}
}
#endif  // defined(__linux__) && defined(__aarch64__)

// ===================================================================
// C++ Operator Definitions
// ===================================================================
// On ARM64 Linux, these use IFUNC for load-time dispatch.
// On x86_64 Linux, getPageSize() is constexpr, so the branch is optimized away.
// On other platforms, they use runtime page size checks.
// ===================================================================

MESH_EXPORT CACHELINE_ALIGNED_FN void *operator new(size_t sz)
#if defined(_GLIBCXX_THROW)
    _GLIBCXX_THROW(std::bad_alloc)
#endif
#if defined(__linux__) && defined(__aarch64__)
        __attribute__((ifunc("resolve_cxx_new")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return cxx_new_impl<kPageSize4K>(sz);
  } else {
    return cxx_new_impl<kPageSize16K>(sz);
  }
}
#endif

MESH_EXPORT CACHELINE_ALIGNED_FN void operator delete(void *ptr) noexcept
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_delete")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    cxx_delete_impl<kPageSize4K>(ptr);
  } else {
    cxx_delete_impl<kPageSize16K>(ptr);
  }
}
#endif

#if !defined(__SUNPRO_CC) || __SUNPRO_CC > 0x420
MESH_EXPORT CACHELINE_ALIGNED_FN void *operator new(size_t sz, const std::nothrow_t &nt) throw()
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_new_nothrow")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return cxx_new_nothrow_impl<kPageSize4K>(sz, nt);
  } else {
    return cxx_new_nothrow_impl<kPageSize16K>(sz, nt);
  }
}
#endif

MESH_EXPORT CACHELINE_ALIGNED_FN void *operator new[](size_t sz)
#if defined(_GLIBCXX_THROW)
    _GLIBCXX_THROW(std::bad_alloc)
#endif
#if defined(__linux__) && defined(__aarch64__)
        __attribute__((ifunc("resolve_cxx_new")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return cxx_new_impl<kPageSize4K>(sz);
  } else {
    return cxx_new_impl<kPageSize16K>(sz);
  }
}
#endif

MESH_EXPORT CACHELINE_ALIGNED_FN void *operator new[](size_t sz, const std::nothrow_t &nt) throw()
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_new_nothrow")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    return cxx_new_nothrow_impl<kPageSize4K>(sz, nt);
  } else {
    return cxx_new_nothrow_impl<kPageSize16K>(sz, nt);
  }
}
#endif

MESH_EXPORT CACHELINE_ALIGNED_FN void operator delete[](void *ptr)
#if defined(_GLIBCXX_USE_NOEXCEPT)
    _GLIBCXX_USE_NOEXCEPT
#else
#if defined(__GNUC__)
    // clang + libcxx on linux
    _NOEXCEPT
#endif
#endif
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_delete")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    cxx_delete_impl<kPageSize4K>(ptr);
  } else {
    cxx_delete_impl<kPageSize16K>(ptr);
  }
}
#endif

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation >= 201309

MESH_EXPORT CACHELINE_ALIGNED_FN void operator delete(void *ptr, size_t sz) noexcept
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_sized_delete")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    cxx_sized_delete_impl<kPageSize4K>(ptr, sz);
  } else {
    cxx_sized_delete_impl<kPageSize16K>(ptr, sz);
  }
}
#endif

MESH_EXPORT CACHELINE_ALIGNED_FN void operator delete[](void *ptr, size_t sz)
#if defined(__GNUC__)
    _GLIBCXX_USE_NOEXCEPT
#endif
#if defined(__linux__) && defined(__aarch64__)
    __attribute__((ifunc("resolve_cxx_sized_delete")));
#else
{
  if (likely(getPageSize() == kPageSize4K)) {
    cxx_sized_delete_impl<kPageSize4K>(ptr, sz);
  } else {
    cxx_sized_delete_impl<kPageSize16K>(ptr, sz);
  }
}
#endif

#endif  // __cpp_sized_deallocation

#endif  // !defined(__SUNPRO_CC) || __SUNPRO_CC > 0x420
#endif  // NEW_INCLUDED

/***** replacement functions for GNU libc extensions to malloc *****/

extern "C" MESH_EXPORT void *MYCDECL CUSTOM_VALLOC(size_t sz) {
  return CUSTOM_MEMALIGN(kPageSize4K, sz);  // Default page size on most architectures.
}

extern "C" MESH_EXPORT void *MYCDECL CUSTOM_PVALLOC(size_t sz) {
  // Rounds up to the next pagesize and then calls valloc. Hoard
  // doesn't support aligned memory requests.
  return CUSTOM_VALLOC((sz + (kPageSize4K - 1)) & ~(kPageSize4K - 1));
}

// The wacky recalloc function, for Windows.
extern "C" MESH_EXPORT void *MYCDECL CUSTOM_RECALLOC(void *p, size_t num, size_t sz) {
  void *ptr = CUSTOM_REALLOC(p, num * sz);
  if ((p == NULL) && (ptr != NULL)) {
    // Clear out the memory.
    memset(ptr, 0, CUSTOM_GETSIZE(ptr));
  }
  return ptr;
}

#if defined(_WIN32)

/////// Other replacement functions that call malloc.

// from http://msdn2.microsoft.com/en-us/library/6ewkz86d(VS.80).aspx
// fgetc, _fgetchar, fgets, fprintf, fputc, _fputchar, fputs, fread, fscanf, fseek, fsetpos
// _fullpath, fwrite, getc, getchar, _getcwd, _getdcwd, gets, _getw, _popen, printf, putc
// putchar, _putenv, puts, _putw, scanf, _searchenv, setvbuf, _strdup, system, _tempnam,
// ungetc, vfprintf, vprintf

char *CUSTOM_GETENV(const char *str) {
  char buf[32767];
  int len = GetEnvironmentVariable(str, buf, 32767);
  if (len == 0) {
    return NULL;
  } else {
    char *str = new char[len + 1];
    strncpy(str, buf, len + 1);
    return str;
  }
}

int CUSTOM_PUTENV(char *str) {
  char *eqpos = strchr(str, '=');
  if (eqpos != NULL) {
    char first[32767], second[32767];
    int namelen = (size_t)eqpos - (size_t)str;
    strncpy(first, str, namelen);
    first[namelen] = '\0';
    int valuelen = strlen(eqpos + 1);
    strncpy(second, eqpos + 1, valuelen);
    second[valuelen] = '\0';
    char buf[255];
    sprintf(buf, "setting %s to %s\n", first, second);
    printf(buf);
    SetEnvironmentVariable(first, second);
    return 0;
  }
  return -1;
}

#endif
