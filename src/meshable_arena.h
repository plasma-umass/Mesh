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

#ifdef __linux__
#define USE_MEMFD 1
#include <linux/fs.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#endif

#if defined(__APPLE__)
#include <copyfile.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_map.h>
#elif defined(__linux__)
#include <sys/sendfile.h>
#endif

#if defined(__FreeBSD__)
#define MADV_DONTDUMP MADV_NOCORE
#define MADV_DUMP MADV_CORE
#endif

#include <sys/ioctl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "internal.h"

#include "cheap_heap.h"

#include "bitmap.h"

#include "mmap_heap.h"

#include "mini_heap.h"

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 0
#endif

#ifndef MADV_DODUMP
#define MADV_DODUMP 0
#endif

namespace mesh {

namespace {
const char *const TMP_DIRS[] = {
    "/dev/shm",
    "/tmp",
};

inline char *uintToStr(char *dst, uint32_t i) {
  constexpr size_t maxLen = sizeof("4294967295") + 1;
  char buf[maxLen];
  memset(buf, 0, sizeof(buf));

  char *digit = buf + maxLen - 2;
  *digit = '0';
  while (i > 0) {
    hard_assert(reinterpret_cast<uintptr_t>(digit) >= reinterpret_cast<uintptr_t>(buf));
    const char c = '0' + (i % 10);
    *digit = c;
    digit--;
    i /= 10;
  }
  if (*digit == '\0') {
    digit++;
  }

  return strcat(dst, digit);
}

template <typename Func>
inline void forEachFree(const internal::vector<Span> freeSpans[kSpanClassCount], const Func func) {
  for (size_t i = 0; i < kSpanClassCount; i++) {
    if (freeSpans[i].empty())
      continue;

    for (size_t j = 0; j < freeSpans[i].size(); j++) {
      auto span = freeSpans[i][j];
      func(span);
    }
  }
}

#ifdef USE_MEMFD
inline int sys_memfd_create(const char *name, unsigned int flags) {
  return syscall(__NR_memfd_create, name, flags);
}
#endif
}  // namespace

template <size_t PageSize>
inline void *&getArenaInstance() {
  static void *instance = nullptr;
  return instance;
}

template <size_t PageSize>
class MeshableArena : public mesh::OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshableArena);
  typedef OneWayMmapHeap SuperHeap;

public:
  static constexpr size_t kPageSizeVal = PageSize;
  static constexpr unsigned kPageShift = __builtin_ctzl(PageSize);
  enum { Alignment = PageSize };

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

  void freePhys(void *ptr, size_t sz);

private:
  void expandArena(size_t minPagesAdded);
  bool findPages(size_t pageCount, Span &result, internal::PageType &type);
  bool ATTRIBUTE_NEVER_INLINE findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], size_t i,
                                             size_t pageCount, Span &result);
  Span reservePages(size_t pageCount, size_t pageAlignment);
  internal::RelaxedBitmap allocatedBitmap(bool includeDirty = true) const;

  void *malloc(size_t sz) = delete;

  inline bool isAligned(const Span &span, const size_t pageAlignment) const {
    return ptrvalFromOffset(span.offset) % (pageAlignment << kPageShift) == 0;
  }

  static constexpr size_t indexSize() {
    return sizeof(Offset) * (kArenaSize / PageSize);
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
        madvise(ptrFromOffset(span.offset), span.length << kPageShift, MADV_DONTDUMP);
      }
      d_assert(span.length > 0);
      _dirty[span.spanClass()].push_back(span);
      _dirtyPageCount += span.length;

      const size_t maxDirtyPageThreshold = (kMaxDirtyPageThreshold * kPageSize4K) / PageSize;

      if (_dirtyPageCount > maxDirtyPageThreshold) {
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

    return (ptrval - arena) >> kPageShift;
  }

  inline uintptr_t ptrvalFromOffset(size_t off) const {
    return reinterpret_cast<uintptr_t>(_arenaBegin) + (off << kPageShift);
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
    auto sz = static_cast<size_t>(span.length) << kPageShift;
    mmap(ptr, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, span.offset << kPageShift);
  }

  void prepareForFork();
  void afterForkParent();
  void afterForkChild();

  void *_arenaBegin{nullptr};
  atomic<MiniHeapID> *_mhIndex{nullptr};

protected:
  CheapHeap<MiniHeapSizeFor<PageSize>(), kArenaSize / PageSize> _mhAllocator{};
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
      kArenaSize / PageSize,
      reinterpret_cast<char *>(OneWayMmapHeap().malloc(bitmap::representationSize(kArenaSize / PageSize))), false};
  size_t _meshedPageCount{0};
  size_t _meshedPageCountHWM{0};
  size_t _rssKbAtHWM{0};
  size_t _maxMeshCount{kDefaultMaxMeshCount};

  int _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};

// Implementation

template <size_t PageSize>
MeshableArena<PageSize>::MeshableArena() : SuperHeap(), _fastPrng(internal::seed(), internal::seed()) {
  d_assert(getArenaInstance<PageSize>() == nullptr);
  getArenaInstance<PageSize>() = this;

  int fd = -1;
  if (kMeshingEnabled) {
    fd = openSpanFile(kArenaSize);
    if (fd < 0) {
      debug("mesh: opening arena file failed.\n");
      abort();
    }
  }
  _fd = fd;

#ifdef __APPLE__
  if (kMeshingEnabled) {
    debug("mesh: using file-backed memory for arena (macOS) - enables F_PUNCHHOLE\n");
  }
  _arenaBegin = SuperHeap::map(kArenaSize, kMapShared, fd);
#else
  _arenaBegin = SuperHeap::map(kArenaSize, kMapShared, fd);
#endif

  _mhIndex = reinterpret_cast<atomic<MiniHeapID> *>(SuperHeap::malloc(indexSize()));

  hard_assert(_arenaBegin != nullptr);
  hard_assert(_mhIndex != nullptr);

  if (kAdviseDump) {
    madvise(_arenaBegin, kArenaSize, MADV_DONTDUMP);
  }

  debug("MeshableArena(%p): fd:%4d\t%p-%p\n", this, _fd, _arenaBegin, arenaEnd());

  atexit(staticAtExit);
  pthread_atfork(staticPrepareForFork, staticAfterForkParent, staticAfterForkChild);
}

template <size_t PageSize>
char *MeshableArena<PageSize>::openSpanDir(int pid) {
  constexpr size_t buf_len = 128;

  for (auto tmpDir : TMP_DIRS) {
    for (size_t i = 0; i < 1024; i++) {
      char buf[buf_len];
      memset(buf, 0, buf_len);

      char *next = buf;
      hard_assert(strlen(tmpDir) < buf_len);
      next = strcat(next, tmpDir);
      next = strcat(next, "/alloc-mesh-");
      next = uintToStr(next, pid);
      next = strcat(next, ".");
      next = uintToStr(next, i);

      hard_assert(reinterpret_cast<uintptr_t>(next) <= reinterpret_cast<uintptr_t>(buf) + buf_len);

      int result = mkdir(buf, 0755);
      if (result != 0) {
        if (errno == EEXIST) {
          continue;
        } else {
          break;
        }
      }

      char *spanDir = reinterpret_cast<char *>(internal::Heap().malloc(strlen(buf) + 1));
      strcpy(spanDir, buf);
      return spanDir;
    }
  }

  return nullptr;
}

template <size_t PageSize>
void MeshableArena<PageSize>::expandArena(size_t minPagesAdded) {
  const size_t pageCount = std::max(minPagesAdded, kMinArenaExpansion);

  Span expansion(_end, pageCount);
  _end += pageCount;

  const size_t maxPages = kArenaSize >> kPageShift;
  if (unlikely(_end >= maxPages)) {
    debug("Mesh: arena exhausted: current arena size is %.1f GB; recompile with larger arena size.",
          kArenaSize / 1024.0 / 1024.0 / 1024.0);
    abort();
  }

  _clean[expansion.spanClass()].push_back(expansion);
}

template <size_t PageSize>
bool MeshableArena<PageSize>::findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], const size_t i,
                                             const size_t pageCount, Span &result) {
  internal::vector<Span> &spanList = freeSpans[i];
  if (spanList.empty())
    return false;

  size_t oldLen = spanList.size();

  if (i == kSpanClassCount - 1 && spanList.back().length < pageCount) {
    for (size_t j = 0; j < spanList.size() - 1; j++) {
      if (spanList[j].length >= pageCount) {
        std::swap(spanList[j], spanList.back());
        break;
      }
    }

    if (spanList.back().length < pageCount) {
      return false;
    }
  }

  Span span = spanList.back();
  spanList.pop_back();

#ifndef NDEBUG
  d_assert_msg(oldLen == spanList.size() + 1, "pageCount:%zu,%zu -- %zu/%zu", pageCount, i, oldLen, spanList.size());
  for (size_t j = 0; j < spanList.size(); j++) {
    d_assert(spanList[j] != span);
  }
#endif

  d_assert(span.length >= i + 1);
  d_assert(span.length >= pageCount);

  Span rest = span.splitAfter(pageCount);
  if (!rest.empty()) {
    freeSpans[rest.spanClass()].push_back(rest);
  }
  d_assert(span.length == pageCount);

  result = span;
  return true;
}

template <size_t PageSize>
bool MeshableArena<PageSize>::findPages(const size_t pageCount, Span &result, internal::PageType &type) {
  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_dirty, i, pageCount, result)) {
      type = internal::PageType::Dirty;
      return true;
    }
  }

  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_clean, i, pageCount, result)) {
      type = internal::PageType::Clean;
      return true;
    }
  }

  return false;
}

template <size_t PageSize>
Span MeshableArena<PageSize>::reservePages(const size_t pageCount, const size_t pageAlignment) {
  d_assert(pageCount >= 1);

  internal::PageType flags(internal::PageType::Unknown);
  Span result(0, 0);
  auto ok = findPages(pageCount, result, flags);
  if (!ok) {
    expandArena(pageCount);
    ok = findPages(pageCount, result, flags);
    hard_assert(ok);
  }

  d_assert(!result.empty());
  d_assert(flags != internal::PageType::Unknown);

  if (unlikely(pageAlignment > 1 && ((ptrvalFromOffset(result.offset) >> kPageShift) % pageAlignment != 0))) {
    freeSpan(result, flags);
    result = reservePages(pageCount + 2 * pageAlignment, 1);

    const size_t alignment = pageAlignment << kPageShift;
    const uintptr_t alignedPtr = (ptrvalFromOffset(result.offset) + alignment - 1) & ~(alignment - 1);
    const auto alignedOff = offsetFor(reinterpret_cast<void *>(alignedPtr));
    d_assert(alignedOff >= result.offset);
    d_assert(alignedOff < result.offset + result.length);
    const auto unwantedPageCount = alignedOff - result.offset;
    auto alignedResult = result.splitAfter(unwantedPageCount);
    d_assert(alignedResult.offset == alignedOff);
    freeSpan(result, flags);
    const auto excess = alignedResult.splitAfter(pageCount);
    freeSpan(excess, flags);
    result = alignedResult;
  }

  return result;
}

template <size_t PageSize>
internal::RelaxedBitmap MeshableArena<PageSize>::allocatedBitmap(bool includeDirty) const {
  internal::RelaxedBitmap bitmap(_end);

  bitmap.setAll(_end);

  auto unmarkPages = [&](const Span &span) {
    for (size_t k = 0; k < span.length; k++) {
#ifndef NDEBUG
      if (!bitmap.isSet(span.offset + k)) {
        debug("arena: bit %zu already unset 1 (%zu/%zu)\n", k, span.offset, span.length);
      }
#endif
      bitmap.unset(span.offset + k);
    }
  };

  if (includeDirty)
    forEachFree(_dirty, unmarkPages);
  forEachFree(_clean, unmarkPages);

  return bitmap;
}

template <size_t PageSize>
char *MeshableArena<PageSize>::pageAlloc(Span &result, size_t pageCount, size_t pageAlignment) {
  if (pageCount == 0) {
    return nullptr;
  }

  d_assert(_arenaBegin != nullptr);

  d_assert(pageCount >= 1);
  d_assert(pageCount < std::numeric_limits<Length>::max());

  auto span = reservePages(pageCount, pageAlignment);
  d_assert(isAligned(span, pageAlignment));

  d_assert(contains(ptrFromOffset(span.offset)));
#ifndef NDEBUG
  if (_mhIndex[span.offset].load().hasValue()) {
    mesh::debug("----\n");
    void *mh_void = miniheapForArenaOffset(span.offset);
    reinterpret_cast<MiniHeap<PageSize> *>(mh_void)->dumpDebug();
  }
#endif

  char *ptr = reinterpret_cast<char *>(ptrFromOffset(span.offset));

  if (kAdviseDump) {
    madvise(ptr, pageCount << kPageShift, MADV_DODUMP);
  }

  result = span;
  return ptr;
}

template <size_t PageSize>
void MeshableArena<PageSize>::free(void *ptr, size_t sz, internal::PageType type) {
  if (unlikely(!contains(ptr))) {
    debug("invalid free of %p/%zu", ptr, sz);
    return;
  }
  d_assert(sz > 0);

  d_assert((sz >> kPageShift) > 0);
  d_assert((sz & (PageSize - 1)) == 0);

  const Span span(offsetFor(ptr), sz >> kPageShift);
  freeSpan(span, type);
}

template <size_t PageSize>
void MeshableArena<PageSize>::partialScavenge() {
  forEachFree(_dirty, [&](const Span &span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    _clean[span.spanClass()].push_back(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i] = internal::vector<Span>{};
  }

  _dirtyPageCount = 0;
}

template <size_t PageSize>
void MeshableArena<PageSize>::scavenge(bool force) {
  const size_t minDirtyPageThreshold = (kMinDirtyPageThreshold * kPageSize4K) / PageSize;

  if (!force && _dirtyPageCount < minDirtyPageThreshold) {
    return;
  }

  auto bitmap = allocatedBitmap(false);
  bitmap.invert();

  auto markPages = [&](const Span &span) {
    for (size_t k = 0; k < span.length; k++) {
#ifndef NDEBUG
      if (bitmap.isSet(span.offset + k)) {
        debug("arena: bit %zu already set (%zu/%zu) %zu\n", k, span.offset, span.length, bitmap.bitCount());
      }
#endif
      bitmap.tryToSet(span.offset + k);
    }
  };

  std::for_each(_toReset.begin(), _toReset.end(), [&](Span span) {
    untrackMeshed(span);
    markPages(span);
    resetSpanMapping(span);
  });

  _toReset = internal::vector<Span>{};

  _meshedPageCount = _meshedBitmap.inUseCount();
  if (_meshedPageCount > _meshedPageCountHWM) {
    _meshedPageCountHWM = _meshedPageCount;
  }

  forEachFree(_dirty, [&](const Span &span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    markPages(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i] = internal::vector<Span>{};
  }

  _dirtyPageCount = 0;

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _clean[i] = internal::vector<Span>{};
  }

  Span current(0, 0);
  for (auto const &i : bitmap) {
    if (i == current.offset + current.length) {
      current.length++;
      continue;
    }

    if (!current.empty()) {
      _clean[current.spanClass()].push_back(current);
    }

    current = Span(i, 1);
  }

  if (!current.empty()) {
    _clean[current.spanClass()].push_back(current);
  }
#ifndef NDEBUG
  auto newBitmap = allocatedBitmap();
  newBitmap.invert();

  const size_t *bits1 = bitmap.bits();
  const size_t *bits2 = newBitmap.bits();
  for (size_t i = 0; i < bitmap.byteCount() / sizeof(size_t); i++) {
    if (bits1[i] != bits2[i]) {
      debug("bitmaps don't match %zu:\n", i);
      hard_assert(false);
    }
  }
#endif
}

template <size_t PageSize>
void MeshableArena<PageSize>::freePhys(void *ptr, size_t sz) {
  d_assert(contains(ptr));
  d_assert(sz > 0);

  d_assert(sz / CPUInfo::PageSize > 0);
  d_assert(sz % CPUInfo::PageSize == 0);

  if (!kMeshingEnabled) {
    return;
  }

  if (_fd == -1) {
    return;
  }

  const off_t off = reinterpret_cast<char *>(ptr) - reinterpret_cast<char *>(_arenaBegin);

#ifdef __FreeBSD__
#if __FreeBSD_version >= 1400000
  struct spacectl_range range = {off, static_cast<off_t>(sz)};
  int result = fspacectl(_fd, SPACECTL_DEALLOC, &range, 0, NULL);
  d_assert_msg(result == 0, "fspacectl(fd %d): %d errno %d (%s)\n", _fd, result, errno, strerror(errno));
#else
#warning "space deallocation unsupported on FreeBSD < 14"
#endif
#elif defined(__APPLE__)
  fpunchhole_t punch;
  memset(&punch, 0, sizeof(punch));
  punch.fp_offset = off;
  punch.fp_length = sz;

  int result = fcntl(_fd, F_PUNCHHOLE, &punch);
  if (result != 0) {
    debug("F_PUNCHHOLE failed (fd %d, off %lld, sz %zu): errno %d (%s)\n", _fd, (long long)off, sz, errno,
          strerror(errno));
  }
#else
  int result = fallocate(_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off, sz);
  d_assert_msg(result == 0, "fallocate(fd %d): %d errno %d (%s)\n", _fd, result, errno, strerror(errno));
#endif
}

template <size_t PageSize>
void MeshableArena<PageSize>::beginMesh(void *keep, void *remove, size_t sz) {
  int r = mprotect(remove, sz, PROT_READ);
  hard_assert(r == 0);
}

template <size_t PageSize>
void MeshableArena<PageSize>::finalizeMesh(void *keep, void *remove, size_t sz) {
  const auto keepOff = offsetFor(keep);
  const auto removeOff = offsetFor(remove);

  const size_t pageCount = sz >> kPageShift;

  const MiniHeapID keepID = _mhIndex[keepOff].load(std::memory_order_acquire);
  for (size_t i = 0; i < pageCount; i++) {
    setIndex(removeOff + i, keepID);
  }

  hard_assert(pageCount < std::numeric_limits<Length>::max());
  const Span removedSpan{removeOff, static_cast<Length>(pageCount)};
  trackMeshed(removedSpan);

#ifdef __APPLE__
  hard_assert(_fd >= 0);
  void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, keepOff << kPageShift);
  hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);
#else
  void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, keepOff << kPageShift);
  hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);
#endif
}

template <size_t PageSize>
int MeshableArena<PageSize>::openShmSpanFile(size_t sz) {
  constexpr size_t buf_len = 64;
  char buf[buf_len];
  memset(buf, 0, buf_len);

  _spanDir = openSpanDir(getpid());
  d_assert(_spanDir != nullptr);

  char *next = strcat(buf, _spanDir);
  strcat(next, "/XXXXXX");

  int fd = mkstemp(buf);
  if (fd < 0) {
    debug("mkstemp: %d (%s)\n", errno, strerror(errno));
    abort();
  }

  int err = unlink(buf);
  if (err != 0) {
    debug("unlink: %d\n", errno);
    abort();
  }

  err = ftruncate(fd, sz);
  if (err != 0) {
    debug("ftruncate: %d\n", errno);
    abort();
  }

  err = fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (err != 0) {
    debug("fcntl: %d\n", errno);
    abort();
  }

  return fd;
}

#ifdef USE_MEMFD
template <size_t PageSize>
int MeshableArena<PageSize>::openSpanFile(size_t sz) {
  errno = 0;
  int fd = sys_memfd_create("mesh_arena", MFD_CLOEXEC);

  if (fd < 0) {
    return openShmSpanFile(sz);
  }

  int err = ftruncate(fd, sz);
  if (err != 0) {
    debug("ftruncate: %d\n", errno);
    abort();
  }

  return fd;
}
#else
template <size_t PageSize>
int MeshableArena<PageSize>::openSpanFile(size_t sz) {
  return openShmSpanFile(sz);
}
#endif  // USE_MEMFD

template <size_t PageSize>
void MeshableArena<PageSize>::staticAtExit() {
  d_assert(getArenaInstance<PageSize>() != nullptr);
  if (getArenaInstance<PageSize>() != nullptr)
    reinterpret_cast<MeshableArena<PageSize> *>(getArenaInstance<PageSize>())->exit();
}

template <size_t PageSize>
void MeshableArena<PageSize>::staticPrepareForFork() {
  d_assert(getArenaInstance<PageSize>() != nullptr);
  reinterpret_cast<MeshableArena<PageSize> *>(getArenaInstance<PageSize>())->prepareForFork();
}

template <size_t PageSize>
void MeshableArena<PageSize>::staticAfterForkParent() {
  d_assert(getArenaInstance<PageSize>() != nullptr);
  reinterpret_cast<MeshableArena<PageSize> *>(getArenaInstance<PageSize>())->afterForkParent();
}

template <size_t PageSize>
void MeshableArena<PageSize>::staticAfterForkChild() {
  d_assert(getArenaInstance<PageSize>() != nullptr);
  reinterpret_cast<MeshableArena<PageSize> *>(getArenaInstance<PageSize>())->afterForkChild();
}

}  // namespace mesh

#endif  // MESH_MESHABLE_ARENA_H
