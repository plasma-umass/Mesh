// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __linux__
#define USE_MEMFD 1
#include <linux/fs.h>
#endif
// #undef USE_MEMFD

#ifdef USE_MEMFD
#include <sys/syscall.h>
#include <unistd.h>

//#include <sys/memfd.h>
#include <asm/unistd_64.h>
#include <linux/memfd.h>
#endif

#include <sys/ioctl.h>

#include <algorithm>

#include "meshable_arena.h"
#include "mini_heap.h"
#include "runtime.h"

namespace mesh {

static void *arenaInstance;

static const char *const TMP_DIRS[] = {
    "/dev/shm",
    "/tmp",
};

MeshableArena::MeshableArena() : SuperHeap(), _fastPrng(internal::seed(), internal::seed()) {
  d_assert(arenaInstance == nullptr);
  arenaInstance = this;

  int fd = -1;
  if (kMeshingEnabled) {
    fd = openSpanFile(kArenaSize);
    if (fd < 0) {
      debug("mesh: opening arena file failed.\n");
      abort();
    }
  }
  _fd = fd;
  _arenaBegin = SuperHeap::map(kArenaSize, kMapShared, fd);
  _mhIndex = reinterpret_cast<atomic<MiniHeapID> *>(SuperHeap::malloc(indexSize()));

  hard_assert(_arenaBegin != nullptr);
  hard_assert(_mhIndex != nullptr);

  if (kAdviseDump) {
    madvise(_arenaBegin, kArenaSize, MADV_DONTDUMP);
  }

  // debug("MeshableArena(%p): fd:%4d\t%p-%p\n", this, fd, _arenaBegin, arenaEnd());

  // TODO: move this to runtime
  atexit(staticAtExit);
  pthread_atfork(staticPrepareForFork, staticAfterForkParent, staticAfterForkChild);
}

char *uintToStr(char *dst, uint32_t i) {
  constexpr size_t maxLen = sizeof("4294967295") + 1;
  char buf[maxLen];
  memset(buf, 0, sizeof(buf));

  char *digit = buf + maxLen - 2;
  // capture the case where i == 0
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

char *MeshableArena::openSpanDir(int pid) {
  constexpr size_t buf_len = 128;

  for (auto tmpDir : TMP_DIRS) {
    for (size_t i = 0; i < 1024; i++) {
      char buf[buf_len];
      memset(buf, 0, buf_len);

      // on some platforms snprintf actually calls out to malloc,
      // despite us passing in a reasonable buffer.  Since what we're doing is
      // reasonably simple, just build the path ourselves to avoid this.
      char *next = buf;
      hard_assert(strlen(tmpDir) < buf_len);
      next = strcat(next, tmpDir);
      next = strcat(next, "/alloc-mesh-");
      next = uintToStr(next, pid);
      next = strcat(next, ".");
      next = uintToStr(next, i);

      // ensure we haven't overflown our buffer
      hard_assert(reinterpret_cast<uintptr_t>(next) <= reinterpret_cast<uintptr_t>(buf) + buf_len);

      int result = mkdir(buf, 0755);
      if (result != 0) {
        if (errno == EEXIST) {
          // we will get EEXIST if we have re-execed -- we need to use a
          // new directory because we could have dropped privileges in
          // the meantime.
          continue;
        } else {
          // otherwise it is likely that the parent tmp directory
          // doesn't exist or we don't have permissions in it.
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

void MeshableArena::expandArena(size_t minPagesAdded) {
  const size_t pageCount = std::max(minPagesAdded, kMinArenaExpansion);

  Span expansion(_end, pageCount);
  _end += pageCount;

  if (unlikely(_end >= kArenaSize / kPageSize)) {
    debug("Mesh: arena exhausted: current arena size is %.1f GB; recompile with larger arena size.",
          kArenaSize / 1024.0 / 1024.0 / 1024.0);
    abort();
  }

  _clean[expansion.spanClass()].push_back(expansion);
}

bool MeshableArena::findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], const size_t i,
                                   const size_t pageCount, Span &result) {
  internal::vector<Span> &spanList = freeSpans[i];
  if (spanList.empty())
    return false;

  size_t oldLen = spanList.size();

  if (i == kSpanClassCount - 1 && spanList.back().length < pageCount) {
    // the final span class contains (and is the only class to
    // contain) variable-size spans, so we need to make sure we
    // search through all candidates in this case.
    for (size_t j = 0; j < spanList.size() - 1; j++) {
      if (spanList[j].length >= pageCount) {
        std::swap(spanList[j], spanList.back());
        break;
      }
    }

    // check that we found something in the above loop. this would be
    // our last loop iteration anyway
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

  // this invariant should be maintained
  d_assert(span.length >= i + 1);
  d_assert(span.length >= pageCount);

  // put the part we don't need back in the reuse pile
  Span rest = span.splitAfter(pageCount);
  if (!rest.empty()) {
    freeSpans[rest.spanClass()].push_back(rest);
  }
  d_assert(span.length == pageCount);

  result = span;
  return true;
}

bool MeshableArena::findPages(const size_t pageCount, Span &result, internal::PageType &type) {
  // Search through all dirty spans first.  We don't worry about
  // fragmenting dirty pages, as being able to reuse dirty pages means
  // we don't increase RSS.
  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_dirty, i, pageCount, result)) {
      type = internal::PageType::Dirty;
      return true;
    }
  }

  // if no dirty pages are available, search clean pages.  An allocated
  // clean page (once it is written to) means an increased RSS.
  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_clean, i, pageCount, result)) {
      type = internal::PageType::Clean;
      return true;
    }
  }

  return false;
}

Span MeshableArena::reservePages(const size_t pageCount, const size_t pageAlignment) {
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

  if (unlikely(pageAlignment > 1 && ((ptrvalFromOffset(result.offset) / kPageSize) % pageAlignment != 0))) {
    freeSpan(result, flags);
    // recurse once, asking for enough extra space that we are sure to
    // be able to find an aligned offset of pageCount pages within.
    result = reservePages(pageCount + 2 * pageAlignment, 1);

    const size_t alignment = pageAlignment * kPageSize;
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

template <typename Func>
static void forEachFree(const internal::vector<Span> freeSpans[kSpanClassCount], const Func func) {
  for (size_t i = 0; i < kSpanClassCount; i++) {
    if (freeSpans[i].empty())
      continue;

    for (size_t j = 0; j < freeSpans[i].size(); j++) {
      auto span = freeSpans[i][j];
      func(span);
    }
  }
}

internal::RelaxedBitmap MeshableArena::allocatedBitmap(bool includeDirty) const {
  internal::RelaxedBitmap bitmap(_end);

  // we can build up a bitmap of in-use pages here by looking at the
  // arena start and end addresses (to compute the number of
  // bits/pages), set all bits to 1, then iterate through our _clean
  // and _dirty lists unsetting pages that aren't in use.

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

char *MeshableArena::pageAlloc(Span &result, size_t pageCount, size_t pageAlignment) {
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
    auto mh = reinterpret_cast<MiniHeap *>(miniheapForArenaOffset(span.offset));
    mh->dumpDebug();
  }
#endif

  char *ptr = reinterpret_cast<char *>(ptrFromOffset(span.offset));

  if (kAdviseDump) {
    madvise(ptr, pageCount * kPageSize, MADV_DODUMP);
  }

  result = span;
  return ptr;
}

void MeshableArena::free(void *ptr, size_t sz, internal::PageType type) {
  if (unlikely(!contains(ptr))) {
    debug("invalid free of %p/%zu", ptr, sz);
    return;
  }
  d_assert(sz > 0);

  d_assert(sz / kPageSize > 0);
  d_assert(sz % kPageSize == 0);

  const Span span(offsetFor(ptr), sz / kPageSize);
  freeSpan(span, type);
}

void MeshableArena::partialScavenge() {
  forEachFree(_dirty, [&](const Span &span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    // don't coalesce, just add to clean
    _clean[span.spanClass()].push_back(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i].clear();
    internal::vector<Span> empty{};
    _dirty[i].swap(empty);
  }

  _dirtyPageCount = 0;
}

void MeshableArena::scavenge(bool force) {
  if (!force && _dirtyPageCount < kMinDirtyPageThreshold) {
    return;
  }

  // the inverse of the allocated bitmap is all of the spans in _clear
  // (since we just MADV_DONTNEED'ed everything in dirty)
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

  // first, untrack the spans in the meshed bitmap and mark them in
  // the (method-local) unallocated bitmap
  std::for_each(_toReset.begin(), _toReset.end(), [&](Span span) {
    untrackMeshed(span);
    markPages(span);
    resetSpanMapping(span);
  });

  // now that we've finally reset to identity all delayed-reset
  // mappings, empty the list
  _toReset.clear();
  {
    // force freeing our internal allocations
    internal::vector<Span> empty{};
    _toReset.swap(empty);
  }

  _meshedPageCount = _meshedBitmap.inUseCount();
  if (_meshedPageCount > _meshedPageCountHWM) {
    _meshedPageCountHWM = _meshedPageCount;
    // TODO: find rss at peak
  }

  forEachFree(_dirty, [&](const Span &span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    markPages(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i].clear();
    internal::vector<Span> empty{};
    _dirty[i].swap(empty);
  }

  _dirtyPageCount = 0;

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _clean[i].clear();
    internal::vector<Span> empty{};
    _clean[i].swap(empty);
  }

  // coalesce adjacent spans
  Span current(0, 0);
  for (auto const &i : bitmap) {
    if (i == current.offset + current.length) {
      current.length++;
      continue;
    }

    // should only be empty the first time/iteration through
    if (!current.empty()) {
      _clean[current.spanClass()].push_back(current);
      // debug("  clean: %4zu/%4zu\n", current.offset, current.length);
    }

    current = Span(i, 1);
  }

  // should only be empty the first time/iteration through
  if (!current.empty()) {
    _clean[current.spanClass()].push_back(current);
    // debug("  clean: %4zu/%4zu\n", current.offset, current.length);
  }
#ifndef NDEBUG
  auto newBitmap = allocatedBitmap();
  newBitmap.invert();

  const size_t *bits1 = bitmap.bits();
  const size_t *bits2 = newBitmap.bits();
  for (size_t i = 0; i < bitmap.byteCount() / sizeof(size_t); i++) {
    if (bits1[i] != bits2[i]) {
      debug("bitmaps don't match %zu:\n", i);
      // debug("\t%s\n", bitmap.to_string().c_str());
      // debug("\t%s\n", newBitmap.to_string().c_str());
      hard_assert(false);
    }
  }
#endif
}

void MeshableArena::freePhys(void *ptr, size_t sz) {
  d_assert(contains(ptr));
  d_assert(sz > 0);

  d_assert(sz / CPUInfo::PageSize > 0);
  d_assert(sz % CPUInfo::PageSize == 0);

  // we madvise(MADV_DONTNEED) elsewhere; this function is only needed
  // when our heap is a shared mapping
  if (!kMeshingEnabled) {
    return;
  }

  const off_t off = reinterpret_cast<char *>(ptr) - reinterpret_cast<char *>(_arenaBegin);
#ifndef __APPLE__
  int result = fallocate(_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off, sz);
  d_assert_msg(result == 0, "result(fd %d): %d errno %d (%s)\n", _fd, result, errno, strerror(errno));
#else
#warning macOS version of fallocate goes here
  fstore_t store = {F_ALLOCATECONTIG, F_PEOFPOSMODE, 0, (long long)sz, 0};
  int result = fcntl(_fd, F_PREALLOCATE, &store);
  if (result == -1) {
    // try and allocate space with fragments
    store.fst_flags = F_ALLOCATEALL;
    result = fcntl(_fd, F_PREALLOCATE, &store);
  }
  // if (result != -1) {
  //    result = ftruncate(_fd, off+sz);
  // }
  d_assert(result == 0);
#endif
}

void MeshableArena::beginMesh(void *keep, void *remove, size_t sz) {
  int r = mprotect(remove, sz, PROT_READ);
  hard_assert(r == 0);
}

void MeshableArena::finalizeMesh(void *keep, void *remove, size_t sz) {
  // debug("keep: %p, remove: %p\n", keep, remove);
  const auto keepOff = offsetFor(keep);
  const auto removeOff = offsetFor(remove);

  const size_t pageCount = sz / kPageSize;
  const MiniHeapID keepID = _mhIndex[keepOff].load(std::memory_order_acquire);
  for (size_t i = 0; i < pageCount; i++) {
    setIndex(removeOff + i, keepID);
  }

  hard_assert(pageCount < std::numeric_limits<Length>::max());
  const Span removedSpan{removeOff, static_cast<Length>(pageCount)};
  trackMeshed(removedSpan);

  void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, keepOff * kPageSize);
  hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);
  freePhys(remove, sz);

  int r = mprotect(remove, sz, PROT_READ | PROT_WRITE);
  hard_assert(r == 0);
}

int MeshableArena::openShmSpanFile(size_t sz) {
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

  // we only need the file descriptors, not the path to the file in the FS
  int err = unlink(buf);
  if (err != 0) {
    debug("unlink: %d\n", errno);
    abort();
  }

  // TODO: see if fallocate makes any difference in performance
  err = ftruncate(fd, sz);
  if (err != 0) {
    debug("ftruncate: %d\n", errno);
    abort();
  }

  // if a new process gets exec'ed, ensure our heap is completely freed.
  err = fcntl(fd, F_SETFD, FD_CLOEXEC);
  if (err != 0) {
    debug("fcntl: %d\n", errno);
    abort();
  }

  return fd;
}

#ifdef USE_MEMFD
static int sys_memfd_create(const char *name, unsigned int flags) {
  return syscall(__NR_memfd_create, name, flags);
}

int MeshableArena::openSpanFile(size_t sz) {
  errno = 0;
  int fd = sys_memfd_create("mesh_arena", MFD_CLOEXEC);
  // the call to memfd failed -- fall back to opening a shm file
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
int MeshableArena::openSpanFile(size_t sz) {
  return openShmSpanFile(sz);
}
#endif  // USE_MEMFD

void MeshableArena::staticAtExit() {
  d_assert(arenaInstance != nullptr);
  if (arenaInstance != nullptr)
    reinterpret_cast<MeshableArena *>(arenaInstance)->exit();
}

void MeshableArena::staticPrepareForFork() {
  d_assert(arenaInstance != nullptr);
  reinterpret_cast<MeshableArena *>(arenaInstance)->prepareForFork();
}

void MeshableArena::staticAfterForkParent() {
  d_assert(arenaInstance != nullptr);
  reinterpret_cast<MeshableArena *>(arenaInstance)->afterForkParent();
}

void MeshableArena::staticAfterForkChild() {
  d_assert(arenaInstance != nullptr);
  reinterpret_cast<MeshableArena *>(arenaInstance)->afterForkChild();
}

void MeshableArena::prepareForFork() {
  if (!kMeshingEnabled) {
    return;
  }

  // debug("%d: prepare fork", getpid());
  runtime().heap().lock();
  runtime().lock();

  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ);
  hard_assert(r == 0);

  int err = pipe(_forkPipe);
  if (err == -1) {
    abort();
  }
}

void MeshableArena::afterForkParent() {
  if (!kMeshingEnabled) {
    return;
  }

  close(_forkPipe[1]);

  char buf[8];
  memset(buf, 0, 8);

  // wait for our child to close + reopen memory.  Without this
  // fence, we may experience memory corruption?

  while (read(_forkPipe[0], buf, 4) == EAGAIN) {
  }
  close(_forkPipe[0]);

  d_assert(strcmp(buf, "ok") == 0);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;

  // only after the child has finished copying the heap is it safe to
  // go back to read/write
  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ | PROT_WRITE);
  hard_assert(r == 0);

  // debug("%d: after fork parent", getpid());
  runtime().unlock();
  runtime().heap().unlock();
}

void MeshableArena::doAfterForkChild() {
  afterForkChild();
}

void MeshableArena::afterForkChild() {
  runtime().updatePid();

  if (!kMeshingEnabled) {
    return;
  }

  // this function can get called twice
  if (_forkPipe[0] == -1) {
    return;
  }

  // debug("%d: after fork child", getpid());
  runtime().unlock();
  runtime().heap().unlock();

  close(_forkPipe[0]);

  char *oldSpanDir = _spanDir;

  // open new file for the arena
  int newFd = openSpanFile(kArenaSize);

  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(newFd, &fileinfo);
  d_assert(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == kArenaSize);

  const int oldFd = _fd;

  const auto bitmap = allocatedBitmap();
  for (auto const &i : bitmap) {
    int result = internal::copyFile(newFd, oldFd, i * kPageSize, kPageSize);
    d_assert(result == CPUInfo::PageSize);
  }

  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ | PROT_WRITE);
  hard_assert(r == 0);

  // remap the new region over the old
  void *ptr = mmap(_arenaBegin, kArenaSize, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, newFd, 0);
  hard_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  // re-do the meshed mappings
  {
    internal::unordered_set<MiniHeap *> seenMiniheaps{};

    for (auto const &i : _meshedBitmap) {
      MiniHeap *mh = reinterpret_cast<MiniHeap *>(miniheapForArenaOffset(i));
      if (seenMiniheaps.find(mh) != seenMiniheaps.end()) {
        continue;
      }
      seenMiniheaps.insert(mh);

      const auto meshCount = mh->meshCount();
      d_assert(meshCount > 1);

      const auto sz = mh->spanSize();
      const auto keep = reinterpret_cast<void *>(mh->getSpanStart(arenaBegin()));
      const auto keepOff = offsetFor(keep);

      const auto base = mh;
      base->forEachMeshed([&](const MiniHeap *mh) {
        if (!mh->isMeshed())
          return false;

        const auto remove = reinterpret_cast<void *>(mh->getSpanStart(arenaBegin()));
        const auto removeOff = offsetFor(remove);

#ifndef NDEBUG
        const Length pageCount = sz / kPageSize;
        for (size_t i = 0; i < pageCount; i++) {
          d_assert(_mhIndex[removeOff + i].load().value() == _mhIndex[keepOff].load().value());
        }
#endif

        void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, _fd, keepOff * kPageSize);

        hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);

        return false;
      });
    }
  }

  _fd = newFd;

  internal::Heap().free(oldSpanDir);

  close(oldFd);

  while (write(_forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
  }
  close(_forkPipe[1]);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;
}
}  // namespace mesh
