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

typedef uint32_t Offset;
typedef uint32_t Length;

struct Span {
  // offset and length are in pages
  explicit Span(Offset _offset, Length _length) : offset(_offset), length(_length) {
  }

  Span(const Span &rhs) : offset(rhs.offset), length(rhs.length) {
  }

  constexpr Span &operator=(const Span &rhs) {
    offset = rhs.offset;
    length = rhs.length;
    return *this;
  }

  Span(Span &&rhs) : offset(rhs.offset), length(rhs.length) {
  }

  bool empty() const {
    return length == 0;
  }

  // reduce the size of this span to pageCount, return another span
  // with the rest of the pages.
  Span splitAfter(Length pageCount) {
    d_assert(pageCount <= length);
    auto restPageCount = length - pageCount;
    length = pageCount;
    return Span(offset + pageCount, restPageCount);
  }

  uint32_t spanClass() const {
    return std::min(length, kSpanClassCount) - 1;
  }

  size_t byteLength() const {
    return length * kPageSize;
  }

  inline bool operator==(const Span &rhs) {
    return offset == rhs.offset && length == rhs.length;
  }

  inline bool operator!=(const Span &rhs) {
    return !(*this == rhs);
  }

  Offset offset;
  Length length;
};

class MeshableArena : public mesh::OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshableArena);
  typedef OneWayMmapHeap SuperHeap;

public:
  enum { Alignment = kPageSize };

  explicit MeshableArena();

  inline bool contains(const void *ptr) const {
    auto arena = reinterpret_cast<uintptr_t>(_arenaBegin);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return arena <= ptrval && ptrval < arena + kArenaSize;
  }

  void *pageAlloc(size_t pageCount, void *owner);
  void free(void *ptr, size_t sz);

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

protected:
  void scavenge();

private:
  void expandArena(Length minPagesAdded);
  bool findPages(internal::vector<Span> freeSpans[kSpanClassCount], Length pageCount, Span &result);
  Span reservePages(Length pageCount);
  void freePhys(void *ptr, size_t sz);
  internal::RelaxedBitmap allocatedBitmap() const;

  void *malloc(size_t sz) = delete;

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

  void *arenaEnd() const {
    return reinterpret_cast<char *>(_arenaBegin) + kArenaSize;
  }

  void *_arenaBegin{nullptr};

  // spans that had been meshed, have been freed, and need to be reset
  // to identity mappings in the page tables.
  internal::vector<Span> _toReset;

  internal::vector<Span> _clean[kSpanClassCount];
  internal::vector<Span> _dirty[kSpanClassCount];

  Offset _end{};  // in pages

  // indexed by offset. no need to be atomic, because protected by
  // _mhRWLock.
  atomic<uintptr_t> *_metadata{nullptr};

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}  // namespace mesh

#endif  // MESH__MESHABLE_ARENA_H
