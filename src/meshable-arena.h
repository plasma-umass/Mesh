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

#include "cheap_heap.h"

#include "bitmap.h"

#include "mmapheap.h"

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 0
#endif

#ifndef MADV_DODUMP
#define MADV_DODUMP 0
#endif

namespace mesh {

namespace internal {

enum PageType {
  Clean = 0,
  Dirty = 1,
  Meshed = 2,
  Unknown = 3,
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

  void free(void *ptr, size_t sz, internal::PageType type);

  inline void *lookupMiniheapOffset(Offset off) const {
    const Offset mhOff = _mhIndex[off];
    const auto result = _mhAllocator.ptrFromOffset(mhOff);
    // debug("lookup ok for (%zu) %zu %p\n", off, mhOff, result);

    return result;
  }

  inline void *lookupMiniheap(const void *ptr) const {
    if (unlikely(!contains(ptr))) {
      return nullptr;
    }

    // we've already checked contains, so we know this offset is
    // within bounds
    const auto off = offsetFor(ptr);

    return lookupMiniheapOffset(off);
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

  // return the maximum number of pages we've had meshed (and thus our
  // savings) at any point in time.
  inline size_t meshedPageHighWaterMark() const {
    return _meshedPageCountHWM;
  }

  inline size_t RSSAtHighWaterMark() const {
    return _rssKbAtHWM;
  }

private:
  void expandArena(Length minPagesAdded);
  bool findPages(Length pageCount, Span &result, internal::PageType &type);
  bool findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], size_t i, Length pageCount, Span &result);
  Span reservePages(Length pageCount, Length pageAlignment);
  void freePhys(void *ptr, size_t sz);
  internal::RelaxedBitmap allocatedBitmap(bool includeDirty = true) const;

  void *malloc(size_t sz) = delete;

  inline bool isAligned(Span span, Length pageAlignment) const {
    return ptrvalFromOffset(span.offset) % (pageAlignment * kPageSize) == 0;
  }

  static constexpr size_t indexSize() {
    // one pointer per page in our arena
    return sizeof(Offset) * (kArenaSize / kPageSize);
  }

  inline void clearIndex(const Span span) {
    for (size_t i = 0; i < span.length; i++) {
      // clear the miniheap pointers we were tracking
      setIndex(span.offset + i, 0);
    }
  }

  inline void freeSpan(Span span, internal::PageType flags) {
    if (span.length == 0) {
      return;
    }

    // this happens when we are trying to get an aligned allocation
    // and returning excess back to the arena
    if (flags == internal::PageType::Clean) {
      _clean[span.spanClass()].push_back(span);
      return;
    }

    clearIndex(span);

    if (flags == internal::PageType::Dirty) {
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
      // delay restoring the identity mapping
      _toReset.push_back(span);
    }
  }

  int openShmSpanFile(size_t sz);
  int openSpanFile(size_t sz);
  char *openSpanDir(int pid);

  // pointer must already have been checked by `contains()` for bounds
  inline Offset offsetFor(const void *ptr) const {
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

  inline void setIndex(size_t off, Offset val) {
    d_assert(off < indexSize());
    _mhIndex[off].store(val, std::memory_order_release);
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
  // indexed by page offset.
  atomic<Offset> *_mhIndex{nullptr};

protected:
  CheapHeap<128, kArenaSize / kPageSize> _mhAllocator{};

private:
  Offset _end{};  // in pages

  // spans that had been meshed, have been freed, and need to be reset
  // to identity mappings in the page tables.
  internal::vector<Span> _toReset;

  internal::vector<Span> _clean[kSpanClassCount];
  internal::vector<Span> _dirty[kSpanClassCount];

  size_t _dirtyPageCount{0};

  internal::RelaxedBitmap _meshedBitmap{
      kArenaSize / kPageSize,
      reinterpret_cast<char *>(OneWayMmapHeap().malloc(bitmap::representationSize(kArenaSize / kPageSize)))};
  size_t _meshedPageCount{0};
  size_t _meshedPageCountHWM{0};
  size_t _rssKbAtHWM{0};
  size_t _maxMeshCount{kDefaultMaxMeshCount};

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}  // namespace mesh

#endif  // MESH__MESHABLE_ARENA_H
