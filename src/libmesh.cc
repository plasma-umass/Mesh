// Copyright 2016 University of Massachusetts, Amherst

#include <stdio.h>   // for sprintf
#include <stdlib.h>  // for abort
#include <unistd.h>  // for write
#include <cstdarg>   // for va_start + friends
#include <cstddef>   // for size_t
#include <new>       // for operator new

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "file-backed-mmap.h"
#include "meshingheap.h"

#include "heaplayers.h"

#include "wrappers/gnuwrapper.cpp"

// The top heap provides memory to back spans managed by MiniHeaps.
class TopHeap : public ExactlyOneHeap<mesh::FileBackedMmapHeap> {};
// The top big heap is called to handle malloc requests for large
// objects.  We define a separate class to handle these to segregate
// bookkeeping for large malloc requests from the ones used to back
// spans (which are allocated from TopHeap)
class TopBigHeap : public ExactlyOneHeap<mesh::MmapHeap> {};

// fewer buckets than regular KingsleyHeap (to ensure multiple objects
// fit in the 128Kb spans used by MiniHeaps).
class BottomHeap : public mesh::MeshingHeap<11, mesh::size2Class, mesh::class2Size, TopHeap, TopBigHeap> {};

// TODO: remove the LockedHeap here and use a per-thread BottomHeap
class CustomHeap : public ANSIWrapper<LockedHeap<PosixLockType, BottomHeap>> {};

inline static CustomHeap *getCustomHeap(void) {
  static char buf[sizeof(CustomHeap)];
  static CustomHeap *heap = new (buf) CustomHeap();

  return heap;
}

// mutex protecting debug and __mesh_assert_fail to avoid concurrent
// use of static buffers by multiple threads
inline static mutex *getAssertMutex(void) {
  static char assertBuf[sizeof(std::mutex)];
  static mutex *assertMutex = new (assertBuf) mutex();

  return assertMutex;
}

// threadsafe printf-like debug statements safe for use in an
// allocator (it will never call into malloc or free to allocate
// memory)
void debug(const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  static char buf[buf_len];
  std::lock_guard<std::mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  int len = vsnprintf(buf, buf_len - 1, fmt, args);
  va_end(args);

  buf[buf_len - 1] = 0;
  if (len > 0) {
    write(STDERR_FILENO, buf, len);
    // ensure a trailing newline is written out
    if (buf[len - 1] != '\n')
      write(STDERR_FILENO, "\n", 1);
  }
}

// out-of-line function called to report an error and exit the program
// when an assertion failed.
void mesh::internal::__mesh_assert_fail(const char *assertion, const char *file, int line, const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  constexpr size_t usr_len = 512;
  static char buf[buf_len];
  static char usr[usr_len];
  std::lock_guard<std::mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  (void)vsnprintf(usr, usr_len - 1, fmt, args);
  va_end(args);

  usr[usr_len - 1] = 0;

  int len = snprintf(buf, buf_len - 1, "%s:%d: ASSERTION '%s' FAILED: %s\n", file, line, assertion, usr);
  if (len > 0) {
    write(STDERR_FILENO, buf, len);
  }

  _exit(EXIT_FAILURE);
}

extern "C" {
void *xxmalloc(size_t sz) {
  return getCustomHeap()->malloc(sz);
}

void xxfree(void *ptr) {
  getCustomHeap()->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return getCustomHeap()->getSize(ptr);
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking
void xxmalloc_lock(void) {
  getCustomHeap()->lock();
}

// ensure we don't concurrently allocate/mess with internal heap data
// structures while forking
void xxmalloc_unlock(void) {
  getCustomHeap()->unlock();
}
}
