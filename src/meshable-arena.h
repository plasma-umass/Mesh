// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MESHABLE_ARENA_H
#define MESH__MESHABLE_ARENA_H

#if defined(_WIN32)
#error "TODO"
#include <windows.h>
#else
// UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <copyfile.h>
#else
#include <sys/sendfile.h>
#endif

#include <new>

#include "internal.h"

#include "bitmap.h"

#include "mmapheap.h"

namespace mesh {

namespace internal {

enum PageType {
  Unallocated = 0,
  Identity = 1,
  Meshed = 2,
};
}  // namespace internal

class MeshableArena : public mesh::OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshableArena);
  typedef OneWayMmapHeap SuperHeap;

public:
  enum { Alignment = MmapWrapper::Alignment };

  explicit MeshableArena();

  inline bool contains(const void *ptr) const {
    auto arena = reinterpret_cast<uintptr_t>(_arenaBegin);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return arena <= ptrval && ptrval < arena + kArenaSize;
  }

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    d_assert(_arenaBegin != nullptr);
    d_assert_msg(sz % HL::CPUInfo::PageSize == 0, "multiple-page allocs only, sz bad: %zu", sz);

    if (sz == HL::CPUInfo::PageSize) {
      size_t off = _bitmap.setFirstEmpty();
      setMetadata(off, internal::PageType::Identity);
      return ptrFromOffset(off);
    }

    const auto pageCount = sz / HL::CPUInfo::PageSize;
    d_assert(pageCount >= 2);
    d_assert(pageCount <= 64);

    uint64_t pattern = 0; // (1 << pageCount) - 1;
    for (size_t i = 0; i < pageCount; i++) {
      pattern |= 1 << i;
    }

    // get a view of the underlying bits as chars
    auto bits = reinterpret_cast<const uint8_t *>(_bitmap.bitmap());
    const auto bitCount = _bitmap.bitCount();

    // debug("searching for %p\n", reinterpret_cast<void *>(pattern));

    size_t byteOff = 0;
    for (; byteOff < (bitCount - pageCount)/8; byteOff++) {
      uint64_t longBits = *reinterpret_cast<const uint64_t *>(bits + byteOff);
      // a contiguous run of pages is available
      if (((~longBits) & pattern) == pattern) {
        break;
      }
    }
    
    const size_t off = byteOff * 8;

    d_assert(getMetadataFlags(off) == 0 && getMetadataPtr(off) == 0);

    // now that we know they are available, set the empty pages to
    // in-use.  This is safe because this whole function is called
    // under the GlobalHeap lock, so there is no chance of concurrent
    // modification between the loop above and the one below.
    for (size_t i = 0; i < pageCount; i++) {
      bool ok = _bitmap.tryToSet(off + i);
      hard_assert(ok);

      d_assert(getMetadataFlags(off + i) == 0 && getMetadataPtr(off + i) == 0);
      setMetadata(off + i, internal::PageType::Identity);
    }

    void *ptr = ptrFromOffset(off);

    if (kAdviseDump) {
      madvise(ptr, sz, MADV_DODUMP);
    }

    return ptr;
  }

  inline void freePhys(void *ptr, size_t sz) {
    d_assert(contains(ptr));
    d_assert(sz > 0);

    d_assert(sz / CPUInfo::PageSize > 0);
    d_assert(sz % CPUInfo::PageSize == 0);

    const off_t off = reinterpret_cast<char *>(ptr) - reinterpret_cast<char *>(_arenaBegin);
#ifndef __APPLE__
    int result = fallocate(_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off, sz);
    d_assert(result == 0);
#else
#warning macOS version of fallocate goes here
#endif
  }

  inline void free(void *ptr, size_t sz) {
    if (unlikely(!contains(ptr))) {
      return;
    }
    d_assert(sz > 0);

    d_assert(sz / CPUInfo::PageSize > 0);
    d_assert(sz % CPUInfo::PageSize == 0);

    const auto off = offsetFor(ptr);
    const uint8_t flags = getMetadataFlags(off);
    if (flags == internal::PageType::Identity) {
      madvise(ptr, sz, MADV_DONTNEED);
      if (kAdviseDump) {
        madvise(ptr, sz, MADV_DONTDUMP);
      }
      freePhys(ptr, sz);
    } else {
      // restore identity mapping
      mmap(ptr, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, _fd, off * CPUInfo::PageSize);
    }

    const auto pageCount = sz / CPUInfo::PageSize;
    for (size_t i = 0; i < pageCount; i++) {
      d_assert(_bitmap.isSet(off + i));
      // clear the miniheap pointers we were tracking
      setMetadata(off + i, 0);
      _bitmap.unset(off + i);
    }

    // debug("in use count after free of %p/%zu: %zu\n", ptr, sz, _bitmap.inUseCount());
  }

  inline void assoc(const void *span, void *miniheap, size_t pageCount) {
    if (unlikely(!contains(span))) {
      mesh::debug("assoc failure %p", span);
      abort();
      return;
    }

    const auto off = offsetFor(span);
    // this non-atomic update is safe because this is only called
    // under the MH writer-lock in global heap
    uintptr_t mh = reinterpret_cast<uintptr_t>(miniheap);
    for (size_t i = 0; i < pageCount; i++) {
      setMetadata(off + i, mh | getMetadataFlags(off));
    }

    // mesh::debug("assoc %p(%zu) %p | %u", span, off, miniheap, getMetadataFlags(off));
  }

  inline void *lookup(const void *ptr) const {
    if (unlikely(!contains(ptr))) {
      return nullptr;
    }

    const auto off = offsetFor(ptr);
    const auto result = reinterpret_cast<void *>(getMetadataPtr(off));
    // mesh::debug("lookup ok for %p(%zu) %p", ptr, off, result);

    return result;
  }

  void beginMesh(void *keep, void *remove, size_t sz);
  void finalizeMesh(void *keep, void *remove, size_t sz);

  const internal::Bitmap &bitmap() const {
    return _bitmap;
  }

private:
  static constexpr inline size_t metadataSize() {
    // one pointer per page in our arena
    return sizeof(uintptr_t) * (kArenaSize / CPUInfo::PageSize);
  }

  int openSpanFile(size_t sz);
  char *openSpanDir(int pid);

  // pointer must already have been checked by `contains()` for bounds
  size_t offsetFor(const void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto arena = reinterpret_cast<uintptr_t>(_arenaBegin);
    return (ptrval - arena) / CPUInfo::PageSize;
  }

  void *ptrFromOffset(size_t off) const {
    return reinterpret_cast<char *>(_arenaBegin) + CPUInfo::PageSize * off;
  }

  inline void setMetadata(size_t off, uintptr_t val) {
    d_assert(off < metadataSize());
    _metadata[off] = val;
  }

  inline uint8_t getMetadataFlags(size_t off) const {
    // flags are stored in the bottom 3 bits of the metadata
    uint8_t flags = _metadata[off] & 0x07;
    return flags;
  }

  inline uintptr_t getMetadataPtr(size_t off) const {
    // flags are stored in the bottom 3 bits of the metadata
    return _metadata[off] & (uintptr_t)~0x07;
  }

  static void staticAtExit();
  static void staticPrepareForFork();
  static void staticAfterForkParent();
  static void staticAfterForkChild();

  void exit() {
    // FIXME: do this from the destructor, and test that destructor is
    // called.  Also don't leak _spanDir
    if (_spanDir != nullptr) {
      rmdir(_spanDir);
      _spanDir = nullptr;
    }
  }

  void prepareForFork();
  void afterForkParent();
  void afterForkChild();

  void *arenaEnd() {
    return reinterpret_cast<char *>(_arenaBegin) + kArenaSize;
  }

  void *_arenaBegin{nullptr};

  // per-page bitmap
  internal::Bitmap _bitmap;

  // indexed by offset
  atomic<uintptr_t> *_metadata{nullptr};

  // internal::vector<DeferredDeallocs> _freelist;

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}  // namespace mesh

#endif  // MESH__MESHABLE_ARENA_H
