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

  if (kAdviseDump) {
    auto ptr = ptrFromOffset(expansion.offset);
    auto sz = expansion.byteLength();
    madvise(ptr, sz, MADV_DODUMP);
  }

  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_dirty[i].empty()) {
  //    debug("_dirty class[%d] = %d\n", i, _dirty[i].size());
  //   }
  // }

  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_clean[i].empty()) {
  //    debug("_clean class[%d] = %d\n", i, _clean[i].size());
  //   }
  // }

  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_dirty[i].empty()) {
  //     debug("_dirty[%d]  size = %d", i, _dirty[i].size());
  //   }
  // }

  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_clean[i].empty()) {
  //     debug("_clean[%d]  size = %d", i, _clean[i].size());
  //   }
  // }

  // for(auto& s : _clean[kSpanClassCount-1]) {
  //   debug("clean(%d, %d)", s.offset, s.length);
  // }
  // debug("expandArena (minPagesAdded=%d) %d, %d,  dirty : %d\n", minPagesAdded, expansion.offset, expansion.length,
  // _dirtyPageCount);

  _clean[expansion.spanClass()].push_back(expansion);
}

bool MeshableArena::findPagesInner(internal::vector<Span> freeSpans[kSpanClassCount], const size_t i,
                                   const size_t pageCount, Span &result) {
  internal::vector<Span> &spanList = freeSpans[i];
  if (spanList.empty())
    return false;

  size_t oldLen = spanList.size();

  if (i == kSpanClassCount - 1) {
    // the final span class contains (and is the only class to
    // contain) variable-size spans, so we need to make sure we
    // search through all candidates in this case.
    for (int64_t j = spanList.size() - 1; j > 0; --j) {
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

  auto ci = rest.spanClass();
  if (!rest.empty()) {
    freeSpans[ci].push_back(rest);
    moveBiggerTofirst(freeSpans[ci]);
  }
  d_assert(span.length == pageCount);

  result = span;
  return true;
}

bool MeshableArena::findPagesInnerFast(internal::vector<Span> freeSpans[kSpanClassCount], const size_t i,
                                       const size_t pageCount, Span &result) {
  internal::vector<Span> &spanList = freeSpans[i];
  if (unlikely(spanList.empty()))
    return false;

  Span span = spanList.back();
  spanList.pop_back();

  // this invariant should be maintained
  d_assert(span.length >= i + 1);
  d_assert(span.length >= pageCount);

  // put the part we don't need back in the reuse pile
  Span rest = span.splitAfter(pageCount);
  if (!rest.empty()) {
    freeSpans[rest.spanClass()].push_back(rest);
    moveBiggerTofirst(freeSpans[rest.spanClass()]);
  }
  d_assert(span.length == pageCount);

  result = span;
  return true;
}

bool MeshableArena::findPages(const size_t pageCount, Span &result, internal::PageType &type) {
  auto targetClass = Span(0, pageCount).spanClass();

  // search the fix length class first
  for (size_t i = targetClass; i < kSpanClassCount - 1; ++i) {
    if (findPagesInnerFast(_dirty, i, pageCount, result)) {
      type = internal::PageType::Dirty;
      _dirtyPageCount -= result.length;
      return true;
    }

    if (findPagesInnerFast(_clean, i, pageCount, result)) {
      type = internal::PageType::Clean;
      return true;
    }
  }

  // Search through all dirty spans first.  We don't worry about
  // fragmenting dirty pages, as being able to reuse dirty pages means
  // we don't increase RSS.
  if (findPagesInner(_dirty, kSpanClassCount - 1, pageCount, result)) {
    type = internal::PageType::Dirty;
    _dirtyPageCount -= result.length;
    return true;
  }

  // if no dirty pages are available, search clean pages.  An allocated
  // clean page (once it is written to) means an increased RSS.
  if (findPagesInner(_clean, kSpanClassCount - 1, pageCount, result)) {
    type = internal::PageType::Clean;
    return true;
  }

  return false;
}

Span MeshableArena::reservePages(const size_t pageCount, const size_t pageAlignment) {
  d_assert(pageCount >= 1);

  internal::PageType flags(internal::PageType::Unknown);
  Span result(0, 0);
  auto ok = findPages(pageCount, result, flags);
  if (!ok) {
    tryAndSendToFree(new internal::FreeCmd(internal::FreeCmd::FLUSH));
    getSpansFromBg(true);
    ok = findPages(pageCount, result, flags);
    if (!ok) {
      expandArena(pageCount);
      ok = findPages(pageCount, result, flags);
      hard_assert(ok);
    }
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

static size_t flushAllSpansToVector(internal::vector<Span> freeSpans[kSpanClassCount],
                                    internal::vector<Span> &flushSpans, size_t needFreeCount) {
  size_t freeCount = 0;

  flushSpans.reserve(needFreeCount);

  for (size_t i = 0; i < kSpanClassCount; ++i) {
    auto &spans = freeSpans[i];
    for (auto &s : spans) {
      flushSpans.emplace_back(s);
      freeCount += s.length;
    }

    spans.clear();
  }

  return freeCount;
}

static size_t flushSpansToVector(internal::vector<Span> freeSpans[kSpanClassCount], internal::vector<Span> &flushSpans,
                                 size_t needFreeCount) {
  size_t freeCount = 0;

  flushSpans.reserve(needFreeCount);

  for (size_t i = 0; i < kSpanClassCount; ++i) {
    auto &spans = freeSpans[i];

    if (spans.empty())
      continue;

    bool clear = true;
    for (int j = spans.size() - 1; j >= 0; --j) {
      if (freeCount < needFreeCount) {
        flushSpans.emplace_back(spans[j]);
        freeCount += spans[j].length;
      } else {
        spans.resize(j + 1);
        clear = false;
        break;
      }
    }

    if (clear) {
      spans.clear();
    }
  }

  return freeCount;
}

struct {
  bool operator()(const Span &a, const Span &b) const {
    return a.length > b.length;
  }
} customLess;

void MeshableArena::getSpansFromBg(bool wait) {
  bool needSort = false;
  bool gotOne = false;

  constexpr unsigned int spin_loops = 16u, spins = 16u;
  unsigned int loop_count = 0;

  while (true) {
    size_t pageCount = 0;
    internal::FreeCmd *preCommand = runtime().getReturnCmdFromBg();
    ++loop_count;

    if (preCommand) {
      // debug("getSpansFromBg = %d\n", preDirtySpans->size());
      // all add to the mark spans
      if (preCommand->cmd == internal::FreeCmd::FLUSH) {
        for (auto &s : preCommand->spans) {
          _clean[s.spanClass()].emplace_back(s);
          pageCount += s.length;
        }

        if (preCommand->spans.size() > 0) {
          needSort = true;
        }
        gotOne = true;
      } else {
        hard_assert(false);
      }
      // debug("getSpansFromBg got %d spans -  %d page from backgroud.\n", preCommand->spans.size(), pageCount);
      delete preCommand;
    } else {
      if (wait && !gotOne && runtime().freeThreadRunning()) {
        if (loop_count < spin_loops) {
          for (unsigned int j = 0; j < spins; ++j) {
            cpupause();
          }
        } else {
          std::this_thread::yield();
        }
        continue;
      } else {
        break;
      }
    }
  }

  if (needSort) {
    std::sort(_clean[kSpanClassCount - 1].begin(), _clean[kSpanClassCount - 1].end(), customLess);
  }
  // debug("getSpansFromBg after sort last");
  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_dirty[i].empty()) {
  //     debug("_dirty[%d]  size = %d", i, _dirty[i].size());
  //   }
  // }

  // for(size_t i = 0; i < kSpanClassCount; ++i) {
  //   if(!_clean[i].empty()) {
  //     debug("_clean[%d]  size = %d", i, _clean[i].size());
  //   }
  // }

  // for(auto& s : _clean[kSpanClassCount-1]) {
  //   debug("clean(%d, %d)", s.offset, s.length);
  // }
  // debug("getSpansFromBg end");
}

void MeshableArena::tryAndSendToFree(internal::FreeCmd *fCommand) {
  auto &rt = runtime();
  bool ok = rt.sendFreeCmd(fCommand);
  while (!ok) {
    getSpansFromBg();
    ok = rt.sendFreeCmd(fCommand);
  }
}

void MeshableArena::partialScavenge() {
  //  getSpansFromBg();
  // always flush 1/2 pages
  size_t needFreeCount = 0;

  if (_dirtyPageCount > kMaxDirtyPageThreshold) {
    needFreeCount = (kMaxDirtyPageThreshold - kMinDirtyPageThreshold) / 5;
  }

  internal::FreeCmd *freeCommand = new internal::FreeCmd(internal::FreeCmd::FREE_DIRTY_PAGE);

  // debug("partialScavenge  need to free needFreeCount = %d\n", needFreeCount);

  // size_t freeCount = flushAllSpansToVector(_dirty, freeCommand->spans, _dirtyPageCount);
  size_t freeCount = flushSpansToVector(_dirty, freeCommand->spans, needFreeCount);

  // debug("partialScavenge _dirtyPageCount = %d  , freeCount = %d , flushSpans->size() =  %d\n", _dirtyPageCount,
  // freeCount, flushSpans->size());
  _dirtyPageCount -= freeCount;

  tryAndSendToFree(freeCommand);
  // tryAndSendToFree(new internal::FreeCmd(internal::FreeCmd::FLUSH));
  // debug("partial FreeCmd::FLUSH");
}

void MeshableArena::scavenge(bool force) {
  if (!force && _dirtyPageCount < kMinDirtyPageThreshold) {
    return;
  }

  internal::FreeCmd *unmapCommand = new internal::FreeCmd(internal::FreeCmd::UNMAP_PAGE);

  auto markPages = [&](const Span &span) {
    // debug("arena:  (%zu/%zu) \n", span.offset, span.length);
    unmapCommand->spans.emplace_back(span);
  };

  // first, untrack the spans in the meshed bitmap and mark them in
  // the (method-local) unallocated bitmap
  std::for_each(_toReset.begin(), _toReset.end(), [&](const Span &span) {
    untrackMeshed(span);
    markPages(span);
    // resetSpanMapping(span);
  });

  // now that we've finally reset to identity all delayed-reset
  // mappings, empty the list
  // debug("_toReset size: %d", _toReset.size());
  _toReset.clear();

  tryAndSendToFree(unmapCommand);

  internal::FreeCmd *freeCommand = new internal::FreeCmd(internal::FreeCmd::FREE_DIRTY_PAGE);
  // dirty page is small, then we don't send the clean page to merge.
  size_t needFreeCount = 0;

  if (_dirtyPageCount > kMaxDirtyPageThreshold) {
    needFreeCount = (kMaxDirtyPageThreshold - kMinDirtyPageThreshold) / 5;
  }
  // size_t freeCount = flushAllSpansToVector(_dirty, freeCommand->spans, _dirtyPageCount);
  size_t freeCount = flushSpansToVector(_dirty, freeCommand->spans, needFreeCount);
  _dirtyPageCount -= freeCount;

  tryAndSendToFree(freeCommand);

  if (freeCount < kMinDirtyPageThreshold) {
    internal::FreeCmd *cleanCommand = new internal::FreeCmd(internal::FreeCmd::CLEAN_PAGE);
    freeCount = flushAllSpansToVector(_clean, cleanCommand->spans, 0);

    tryAndSendToFree(cleanCommand);
    // debug("FreeCmd::CLEAN_PAGE");
  }

  tryAndSendToFree(new internal::FreeCmd(internal::FreeCmd::FLUSH));

  getSpansFromBg();
}

void MeshableArena::freePhys(const Span &span) {
  auto ptr = ptrFromOffset(span.offset);
  auto sz = span.byteLength();
  freePhys(ptr, sz);
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
  internal::Heap().lock();

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

  internal::Heap().unlock();

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
  internal::Heap().unlock();
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

  if (kAdviseDump) {
    madvise(_arenaBegin, kArenaSize, MADV_DONTDUMP);
  }

  // re-do the meshed mappings
  {
    internal::unordered_set<MiniHeap *> seenMiniheaps{};

    for (auto const &i : _meshedBitmap) {
      MiniHeap *mh = reinterpret_cast<MiniHeap *>(miniheapForArenaOffset(i));
      if (!mh || seenMiniheaps.find(mh) != seenMiniheaps.end()) {
        continue;
      }
      seenMiniheaps.insert(mh);

      const auto meshCount = mh->meshCount();
      if (meshCount == 1) {
        continue;
      }

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

        void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, newFd, keepOff * kPageSize);

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
