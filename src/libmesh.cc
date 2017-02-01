// Copyright 2016 University of Massachusetts, Amherst

#include <stdio.h>   // for sprintf
#include <stdlib.h>  // for abort
#include <unistd.h>  // for write
#include <cstddef>   // for size_t
#include <new>       // for operator new

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)

#define MALLOC_TRACE 0

#include "bitmap.h"
#include "heaplayers.h"
#include "rng/mwc.h"

#include "file-backed-mmap.h"
#include "meshingheap.h"

#include "wrappers/gnuwrapper.cpp"

using namespace HL;
using namespace mesh;

// mmaps over named files
class TopHeap : public ExactlyOneHeap<LockedHeap<PosixLockType, FileBackedMmapHeap<internal::Heap>>> {};
// anon mmaps
class TopBigHeap : public ExactlyOneHeap<LockedHeap<PosixLockType, MmapHeap>> {};

// fewer buckets than regular KingsleyHeap (to ensure multiple objects fit in the 128Kb spans used by MiniHeaps).
class BottomHeap : public MeshingHeap<12, Kingsley::size2Class, Kingsley::class2Size, TopHeap, TopBigHeap> {};

// TODO: remove the LockedHeap here and use a per-thread BottomHeap
class CustomHeap : public ANSIWrapper<LockedHeap<PosixLockType, BottomHeap>> {};

// thread_local CustomHeap *perThreadHeap;

// inline static CustomHeap *getCustomHeap(void) {
//   if (!perThreadHeap) {
//     void *buf = InternalHeap().malloc(sizeof(CustomHeap));
//     if (!buf)
//       abort();
//     perThreadHeap = new (buf) CustomHeap();
//   }
//   return perThreadHeap;
// }

inline static CustomHeap *getCustomHeap(void) {
  static char buf[sizeof(CustomHeap)];
  static CustomHeap *heap = new (buf) CustomHeap();

  return heap;
}

// non-threadsafe printf-like debug statements
void debug(const char *fmt, ...) {
  static char buf[256];
  va_list args;

  va_start(args, fmt);
  int len = vsnprintf(buf, 255, fmt, args);
  va_end(args);

  buf[255] = 0;
  if (len > 0)
    write(STDERR_FILENO, buf, len);
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

void xxmalloc_lock() {
}

void xxmalloc_unlock() {
}
}
