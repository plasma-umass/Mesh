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

  void *pageAlloc(size_t pageCount, void *owner, size_t pageAlignment = 1);
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

  inline bool aboveMeshThreshold() const {
    return _meshedPageCount > _maxMeshCount;
  }

  inline void setMaxMeshCount(size_t maxMeshCount) {
    // debug("setting max map count: %zu", maxMeshCount);
    _maxMeshCount = maxMeshCount;
  }

  inline size_t maxMeshCount() const {
    return _maxMeshCount;
  }

  // protected:
  // public for testing
  void scavenge();
  // like a scavenge, but we only MADV_FREE
  void partialScavenge();

  inline bool maybeScavenge() {
    if (_dirtyPageCount <= kMaxDirtyPageThreshold) {
      return false;
    }

    partialScavenge();
    return true;
  }

private:
  void expandArena(Length minPagesAdded);
  bool findPages(Length pageCount, Span &result);
  bool findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], size_t i, Length pageCount, Span &result);
  Span reservePages(Length pageCount, Length pageAlignment);
  void freePhys(void *ptr, size_t sz);
  internal::RelaxedBitmap allocatedBitmap(bool includeDirty = true) const;

  void *malloc(size_t sz) = delete;

  inline bool isAligned(Span span, Length pageAlignment) const {
    return ptrvalFromOffset(span.offset) % (pageAlignment * kPageSize) == 0;
  }

  static constexpr size_t metadataSize() {
    // one pointer per page in our arena
    return sizeof(uintptr_t) * (kArenaSize / kPageSize);
  }

  inline void freeSpan(Span span) {
    if (span.length == 0) {
      return;
    }

    const uint8_t flags = getMetadataFlags(span.offset);
    // this happens when we are trying to get an aligned allocation
    // and returning excess back to the arena
    if (flags == internal::PageType::Unallocated) {
      _clean[span.spanClass()].push_back(span);
      return;
    }

    for (size_t i = 0; i < span.length; i++) {
      // clear the miniheap pointers we were tracking
      setMetadata(span.offset + i, 0);
    }

    if (flags == internal::PageType::Identity) {
      if (kAdviseDump) {
        madvise(ptrFromOffset(span.offset), span.length * kPageSize, MADV_DONTDUMP);
      }
      d_assert(span.length > 0);
      _dirty[span.spanClass()].push_back(span);
      _dirtyPageCount += span.length;
      if (_dirtyPageCount > kMaxDirtyPageThreshold) {
        partialScavenge();
      }
    } else if (flags == internal::PageType::Meshed) {
      // debug("delaying resetting meshed mapping\n");
      // delay restoring the identity mapping
      _toReset.push_back(span);
      // resetSpanMapping(span);
      // _clean[span.spanClass()].push_back(span);
    }

    // debug("in use count after free of %p/%zu: %zu\n", ptr, sz, _bitmap.inUseCount());
  }

  int openShmSpanFile(size_t sz);
  int openSpanFile(size_t sz);
  char *openSpanDir(int pid);

  // pointer must already have been checked by `contains()` for bounds
  inline Length offsetFor(const void *ptr) const {
    const uintptr_t ptrval = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t arena = reinterpret_cast<uintptr_t>(_arenaBegin);

    d_assert(ptrval >= arena);

    return (ptrval - arena) / kPageSize;
  }

  inline uintptr_t ptrvalFromOffset(size_t off) const {
    return reinterpret_cast<uintptr_t>(_arenaBegin) + off * kPageSize;
  }

  inline void *ptrFromOffset(size_t off) const {
    return reinterpret_cast<void *>(ptrvalFromOffset(off));
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

  inline void trackMeshed(Span span) {
    for (size_t i = 0; i < span.length; i++) {
      // this may already be 1 if it was a meshed virtual span that is
      // now being re-meshed to a new owning miniheap
      _meshedBitmap.tryToSet(span.offset + i);
    }
  }

  inline void untrackMeshed(Span span) {
    for (size_t i = 0; i < span.length; i++) {
      d_assert(_meshedBitmap.isSet(span.offset + i));
      _meshedBitmap.unset(span.offset + i);
    }
  }

  inline void resetSpanMapping(Span span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    mmap(ptr, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, _fd, span.offset * kPageSize);
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

  size_t _dirtyPageCount{0};

  Offset _end{};  // in pages

  internal::RelaxedBitmap _meshedBitmap{
      kArenaSize / kPageSize,
      reinterpret_cast<char *>(OneWayMmapHeap().malloc(bitmap::representationSize(kArenaSize / kPageSize)))};
  size_t _meshedPageCount{0};
  size_t _maxMeshCount{kDefaultMaxMeshCount};

  // indexed by offset. no need to be atomic, because protected by
  // _mhRWLock.
  uintptr_t *_metadata{nullptr};

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}  // namespace mesh

#endif  // MESH__MESHABLE_ARENA_H
