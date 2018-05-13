// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

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

#include "meshable-arena.h"

#include "miniheap.h"

#include "runtime.h"

namespace mesh {

static void *arenaInstance;

static const char *const TMP_DIRS[] = {
    "/dev/shm",
    "/tmp",
};

MeshableArena::MeshableArena() : SuperHeap() {
  d_assert(arenaInstance == nullptr);
  arenaInstance = this;

  int fd = openSpanFile(kArenaSize);
  if (fd < 0) {
    debug("mesh: opening arena file failed.\n");
    abort();
  }
  _fd = fd;
  _arenaBegin = SuperHeap::map(kArenaSize, MAP_SHARED, fd);
  _mhIndex = reinterpret_cast<atomic<Offset> *>(SuperHeap::malloc(indexSize()));

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

char *MeshableArena::openSpanDir(int pid) {
  constexpr size_t buf_len = 128;

  for (auto tmpDir : TMP_DIRS) {
    for (size_t i = 0; i < 1024; i++) {
      char buf[buf_len];
      memset(buf, 0, buf_len);

      snprintf(buf, buf_len - 1, "%s/alloc-mesh-%d.%zud", tmpDir, pid, i);
      int result = mkdir(buf, 0755);
      if (result != 0) {
        if (errno == EEXIST) {
          // we will get EEXIST if we have re-execed -- we need to use a
          // new directory because we could have dropped priviledges in
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

void MeshableArena::expandArena(Length minPagesAdded) {
  const size_t pageCount = std::max(minPagesAdded, kMinArenaExpansion);

  Span expansion(_end, pageCount);
  _end += pageCount;

  _clean[expansion.spanClass()].push_back(expansion);
}

bool MeshableArena::findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], size_t i, Length pageCount,
                                   Span &result) {
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

    // check that we found something in the above loop.this would be
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

  bool MeshableArena::findPages(Length pageCount, Span &result, internal::PageType &type) {
  // Search through all dirty spans first.  We don't worry about
  // fragmenting dirty pages, as being able to reuse dirty pages means
  // we don't increase RSS.
  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_dirty, i, pageCount, result)) {
      type = internal::PageType::Dirty;
      return true;
    }
  }

  // if no dirty pages are avaiable, search clean pages.  An allocated
  // clean page (once it is written to) means an increased RSS.
  for (size_t i = Span(0, pageCount).spanClass(); i < kSpanClassCount; i++) {
    if (findPagesInner(_clean, i, pageCount, result)) {
      type = internal::PageType::Clean;
      return true;
    }
  }

  return false;
}

Span MeshableArena::reservePages(Length pageCount, Length pageAlignment) {
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

  bitmap.setAll();
  
  auto unmarkPages = [&](const Span span) {
    for (size_t k = 0; k < span.length; k++) {
#ifdef NDEBUG
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

void *MeshableArena::pageAlloc(size_t pageCount, void *owner, size_t pageAlignment) {
  if (pageCount == 0) {
    return nullptr;
  }

  d_assert(_arenaBegin != nullptr);

  d_assert(pageCount >= 1);
  d_assert(pageCount < std::numeric_limits<Length>::max());

  auto span = reservePages(pageCount, pageAlignment);
  d_assert((reinterpret_cast<uintptr_t>(ptrFromOffset(span.offset)) / kPageSize) % pageAlignment == 0);

  const auto off = span.offset;

  d_assert(ptrFromOffset(span.offset) < arenaEnd());
#ifndef NDEBUG
  if (lookupMiniheapOffset(off) != _mhAllocator.arenaBegin()) {
    mesh::debug("----\n");
    auto mh = reinterpret_cast<MiniHeap *>(lookupMiniheapOffset(off));
    mh->dumpDebug();
  }
#endif

  const auto ownerVal = _mhAllocator.offsetFor(owner);

  // now that we know they are available, set the empty pages to
  // in-use.  This is safe because this whole function is called
  // under the GlobalHeap lock, so there is no chance of concurrent
  // modification between the loop above and the one below.
  for (size_t i = 0; i < pageCount; i++) {
#ifndef NDEBUG
    if (lookupMiniheapOffset(off + i) != _mhAllocator.arenaBegin()) {
      mesh::debug("----!\n");
      auto mh = reinterpret_cast<MiniHeap *>(lookupMiniheapOffset(off + i));
      mh->dumpDebug();
    }
#endif
    setIndex(off + i, ownerVal);
  }

  void *ptr = ptrFromOffset(off);

  if (kAdviseDump) {
    madvise(ptr, pageCount * kPageSize, MADV_DODUMP);
  }

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
  forEachFree(_dirty, [&](const Span span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    // don't coalesce, just add to clean
    _clean[span.spanClass()].push_back(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i].clear();
  }

  _dirtyPageCount = 0;
}

void MeshableArena::scavenge() {
  // the inverse of the allocated bitmap is all of the spans in _clear
  // (since we just MADV_DONTNEED'ed everything in dirty)
  auto bitmap = allocatedBitmap(false);
  bitmap.invert();

  auto markPages = [&](const Span span) {
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
  std::for_each(_toReset.begin(), _toReset.end(), [&](const Span span) {
    untrackMeshed(span);
    markPages(span);
  });

  {
    internal::unordered_set<Offset> remappedStarts{};

    std::for_each(_toReset.begin(), _toReset.end(), [&](const Span span) {
      Offset startMeshedOffset = _meshedBitmap.highestSetBitBeforeOrAt(span.offset);
      // might be 0
      if (_meshedBitmap.isSet(startMeshedOffset)) {
        startMeshedOffset++;
      }
      Offset nextMeshedOffset = _meshedBitmap.lowestSetBitAt(span.offset + span.length);
      if (nextMeshedOffset > _end) {
        nextMeshedOffset = _end;
      }
      const Span expandedSpan{startMeshedOffset, nextMeshedOffset - startMeshedOffset};

      if (remappedStarts.find(expandedSpan.offset) != remappedStarts.end()) {
        return;
      }
      // debug("resetting mapping for %u-%u (%u pages, originally %u) %zu meshed pages", expandedSpan.offset,
      //       expandedSpan.offset + expandedSpan.length, expandedSpan.length, span.length, _meshedPageCount);

      remappedStarts.insert(expandedSpan.offset);
      resetSpanMapping(expandedSpan);
    });
  }

  // now that we've finally reset to identity all delayed-reset
  // mappings, empty the list
  _toReset.clear();

  _meshedPageCount = _meshedBitmap.inUseCount();
  if (_meshedPageCount > _meshedPageCountHWM) {
    _meshedPageCountHWM = _meshedPageCount;
    // TODO: find rss at peak
  }

  forEachFree(_dirty, [&](const Span span) {
    auto ptr = ptrFromOffset(span.offset);
    auto sz = span.byteLength();
    madvise(ptr, sz, MADV_DONTNEED);
    freePhys(ptr, sz);
    markPages(span);
  });

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _dirty[i].clear();
  }

  _dirtyPageCount = 0;

  for (size_t i = 0; i < kSpanClassCount; i++) {
    _clean[i].clear();
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
      debug("\t%s\n", bitmap.to_string().c_str());
      debug("\t%s\n", newBitmap.to_string().c_str());
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

  const off_t off = reinterpret_cast<char *>(ptr) - reinterpret_cast<char *>(_arenaBegin);
#ifndef __APPLE__
  int result = fallocate(_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, off, sz);
  d_assert(result == 0);
#else
#warning macOS version of fallocate goes here
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

  const Length pageCount = sz / kPageSize;
  const Offset keepMHOff = _mhIndex[keepOff];
  for (size_t i = 0; i < pageCount; i++) {
    // TODO: remove duplication of meshed metadata between the low
    // bits here and the meshed bitmap
    setIndex(removeOff + i, keepMHOff);
  }

  const Span removedSpan{removeOff, pageCount};
  trackMeshed(removedSpan);

  void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, _fd, keepOff * kPageSize);
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

  sprintf(buf, "%s/XXXXXX", _spanDir);

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
  // debug("%d: prepare fork", getpid());
  runtime().heap().lock();
  runtime().lock();

  int err = pipe(_forkPipe);
  if (err == -1)
    abort();
}

void MeshableArena::afterForkParent() {
  // debug("%d: after fork parent", getpid());
  runtime().unlock();
  runtime().heap().unlock();

  close(_forkPipe[1]);

  char buf[8];
  memset(buf, 0, 8);

  // wait for our child to close + reopen memory.  Without this
  // fence, we may experience memory corruption?

  while (read(_forkPipe[0], buf, 4) == EAGAIN) {
  }
  close(_forkPipe[0]);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;

  d_assert(strcmp(buf, "ok") == 0);

  runtime().unlock();
  runtime().heap().unlock();
}

void MeshableArena::afterForkChild() {
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

  // remap the new region over the old
  void *ptr = mmap(_arenaBegin, kArenaSize, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, newFd, 0);
  hard_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  // re-do the meshed mappings
  {
    internal::unordered_set<MiniHeap *> seenMiniheaps{};

    for (auto const &i : _meshedBitmap) {
      MiniHeap *mh = reinterpret_cast<MiniHeap *>(lookupMiniheapOffset(i));
      if (seenMiniheaps.find(mh) != seenMiniheaps.end()) {
        continue;
      }
      seenMiniheaps.insert(mh);

      const auto meshCount = mh->meshCount();
      d_assert(meshCount > 1);

      const auto sz = mh->spanSize();
      const auto keep = mh->spans()[0];
      const auto keepOff = offsetFor(keep);

      for (size_t j = 1; j < meshCount; j++) {
        const auto remove = mh->spans()[j];
        const auto removeOff = offsetFor(remove);

#ifndef NDEBUG
        const Length pageCount = sz / kPageSize;
        for (size_t i = 0; i < pageCount; i++) {
          d_assert(_mhIndex[removeOff + i] == _mhIndex[keepOff]);
        }
#endif

        void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, _fd, keepOff * kPageSize);

        hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);
      }
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

  runtime().unlock();
  runtime().heap().unlock();
}
}  // namespace mesh
