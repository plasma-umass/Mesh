// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_MESHABLE_ARENA_H
#define MESH_MESHABLE_ARENA_H

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

#include "mmap_heap.h"

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 0
#endif

#ifndef MADV_DODUMP
#define MADV_DODUMP 0
#endif

namespace mesh {

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

  char *pageAlloc(Span &result, size_t pageCount, size_t pageAlignment = 1);

  void free(void *ptr, size_t sz, internal::PageType type);

  inline void trackMiniHeap(const Span span, MiniHeapID id) {
    // now that we know they are available, set the empty pages to
    // in-use.  This is safe because this whole function is called
    // under the GlobalHeap lock, so there is no chance of concurrent
    // modification between the loop above and the one below.
    for (size_t i = 0; i < span.length; i++) {
#ifndef NDEBUG
      d_assert(!_mhIndex[span.offset + i].load(std::memory_order_acquire).hasValue());
      // auto mh = reinterpret_cast<MiniHeap *>(miniheapForArenaOffset(span.offset + i));
      // mh->dumpDebug();
#endif
      setIndex(span.offset + i, id);
    }
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE miniheapForArenaOffset(Offset arenaOff) const {
    const MiniHeapID mhOff = _mhIndex[arenaOff].load(std::memory_order_acquire);
    if (likely(mhOff.hasValue())) {
      return _mhAllocator.ptrFromOffset(mhOff.value());
    }

    return nullptr;
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE lookupMiniheap(const void *ptr) const {
    if (unlikely(!contains(ptr))) {
      return nullptr;
    }

    // we've already checked contains, so we know this offset is
    // within bounds
    const auto arenaOff = offsetFor(ptr);
    return miniheapForArenaOffset(arenaOff);
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
  void scavenge(bool force);
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

  char *arenaBegin() const {
    return reinterpret_cast<char *>(_arenaBegin);
  }
  void *arenaEnd() const {
    return reinterpret_cast<char *>(_arenaBegin) + kArenaSize;
  }

  void doAfterForkChild();

private:
  void expandArena(size_t minPagesAdded);
  bool findPages(size_t pageCount, Span &result, internal::PageType &type);
  bool ATTRIBUTE_NEVER_INLINE findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], size_t i,
                                             size_t pageCount, Span &result);
  Span reservePages(size_t pageCount, size_t pageAlignment);
  void freePhys(void *ptr, size_t sz);
  internal::RelaxedBitmap allocatedBitmap(bool includeDirty = true) const;

  void *malloc(size_t sz) = delete;

  inline bool isAligned(const Span &span, const size_t pageAlignment) const {
    return ptrvalFromOffset(span.offset) % (pageAlignment * kPageSize) == 0;
  }

  static constexpr size_t indexSize() {
    // one pointer per page in our arena
    return sizeof(Offset) * (kArenaSize / kPageSize);
  }

  inline void clearIndex(const Span &span) {
    for (size_t i = 0; i < span.length; i++) {
      // clear the miniheap pointers we were tracking
      setIndex(span.offset + i, MiniHeapID{0});
    }
  }

  inline void freeSpan(const Span &span, const internal::PageType flags) {
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
        // do a full scavenge with a probability 1/10
        if (_fastPrng.inRange(0, 9) == 9) {
          scavenge(true);
        } else {
          partialScavenge();
        }
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

  inline void setIndex(size_t off, MiniHeapID val) {
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

  inline void trackMeshed(const Span &span) {
    for (size_t i = 0; i < span.length; i++) {
      // this may already be 1 if it was a meshed virtual span that is
      // now being re-meshed to a new owning miniheap
      _meshedBitmap.tryToSet(span.offset + i);
    }
  }

  inline void untrackMeshed(const Span &span) {
    for (size_t i = 0; i < span.length; i++) {
      d_assert(_meshedBitmap.isSet(span.offset + i));
      _meshedBitmap.unset(span.offset + i);
    }
  }

  inline void resetSpanMapping(const Span &span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    mmap(ptr, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, span.offset * kPageSize);
  }

  void prepareForFork();
  void afterForkParent();
  void afterForkChild();

  void *_arenaBegin{nullptr};
  // indexed by page offset.
  atomic<MiniHeapID> *_mhIndex{nullptr};

protected:
  CheapHeap<64, kArenaSize / kPageSize> _mhAllocator{};
  MWC _fastPrng;

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
      reinterpret_cast<char *>(OneWayMmapHeap().malloc(bitmap::representationSize(kArenaSize / kPageSize))), false};
  size_t _meshedPageCount{0};
  size_t _meshedPageCountHWM{0};
  size_t _rssKbAtHWM{0};
  size_t _maxMeshCount{kDefaultMaxMeshCount};

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}  // namespace mesh

#endif  // MESH_MESHABLE_ARENA_H
