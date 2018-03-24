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

#ifndef USE_MEMFD
  _spanDir = openSpanDir(getpid());
  d_assert(_spanDir != nullptr);
#endif

  int fd = openSpanFile(kArenaSize);
  if (fd < 0) {
    debug("mesh: opening arena file failed.\n");
    abort();
  }
  _fd = fd;
  _arenaBegin = SuperHeap::map(kArenaSize, MAP_SHARED, fd);
  _metadata = reinterpret_cast<atomic<uintptr_t> *>(SuperHeap::map(metadataSize(), MAP_ANONYMOUS | MAP_PRIVATE));

  hard_assert(_arenaBegin != nullptr);
  hard_assert(_metadata != nullptr);

  if (kAdviseDump) {
    madvise(_arenaBegin, kArenaSize, MADV_DONTDUMP);
  }

  // debug("MeshableArena(%p): fd:%4d\t%p-%p\n", this, fd, _arenaBegin, arenaEnd());

  // TODO: move this to runtime
  atexit(staticAtExit);
  pthread_atfork(staticPrepareForFork, staticAfterForkParent, staticAfterForkChild);
}

char *MeshableArena::openSpanDir(int pid) {
  constexpr size_t buf_len = 64;

  for (auto tmpDir : TMP_DIRS) {
    char buf[buf_len];
    memset(buf, 0, buf_len);

    snprintf(buf, buf_len - 1, "%s/alloc-mesh-%d", tmpDir, pid);
    int result = mkdir(buf, 0755);
    // we will get EEXIST if we have re-execed
    if (result != 0 && errno != EEXIST)
      continue;

    char *spanDir = reinterpret_cast<char *>(internal::Heap().malloc(strlen(buf) + 1));
    strcpy(spanDir, buf);
    return spanDir;
  }

  return nullptr;
}

void MeshableArena::expandArena(Length minPagesAdded) {
  if (minPagesAdded < kMinArenaExpansion)
    minPagesAdded = kMinArenaExpansion;

  Span expansion(_end, minPagesAdded);
  _end += minPagesAdded;

  _clean[expansion.spanClass()].push_back(expansion);
}

bool MeshableArena::findPages(internal::vector<Span> freeSpans[kSpanClassCount], Length pageCount, Span &result) {
  auto found = false;
  for (size_t i = pageCount - 1; i < kSpanClassCount; i++) {
    if (freeSpans[i].empty())
      continue;

    Span span = freeSpans[i].back();
    freeSpans[i].pop_back();

    // this invariant should be maintained
    d_assert(span.length >= i + 1);
    d_assert(span.length >= pageCount);

    // put the part we don't need back in the reuse pile
    Span rest = span.split(pageCount);
    if (!rest.empty()) {
      freeSpans[rest.spanClass()].push_back(rest);
    }

    result = span;
    found = true;
    break;
  }

  return found;
}

Span MeshableArena::reservePages(Length pageCount) {
  d_assert(pageCount >= 1);

  Span result(0, 0);
  auto ok = findPages(_dirty, pageCount, result);
  if (!ok) {
    ok = findPages(_clean, pageCount, result);
  }
  if (!ok) {
    expandArena(pageCount);
    ok = findPages(_clean, pageCount, result);
    hard_assert(ok);
  }

  d_assert(!result.empty());

  return result;
}

void MeshableArena::freeSpan(Span span) {
  d_assert(span.length > 0);
  _dirty[span.spanClass()].push_back(span);
}

template<typename Func>
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

internal::Bitmap MeshableArena::allocatedBitmap() const {
  Bitmap bitmap(_end);

  // we can build up a bitmap of in-use pages here by looking at the
  // arena start and end addresses (to compute the number of
  // bits/pages), set all bits to 1, then iterate through our _clean
  // and _dirty lists unsetting pages that aren't in use.

  // TODO: add a 'set all' method to bitmap?
  for (size_t i = 0; i < bitmap.bitCount(); i++) {
    bitmap.tryToSet(i);
  }

  auto unmarkPages = [&] (const Span span) {
    for (size_t k = 0; k < span.length; k++) {
      bitmap.unset(span.offset + k);
    }
  };

  forEachFree(_dirty, unmarkPages);
  forEachFree(_clean, unmarkPages);

  return bitmap;
}

void *MeshableArena::malloc(size_t sz) {
  if (sz == 0)
    return nullptr;

  d_assert(_arenaBegin != nullptr);
  d_assert_msg(sz % HL::CPUInfo::PageSize == 0, "multiple-page allocs only, sz bad: %zu", sz);

  const auto pageCount = sz / HL::CPUInfo::PageSize;
  d_assert(pageCount >= 1);
  d_assert(pageCount < std::numeric_limits<Length>::max());

  auto span = reservePages(pageCount);
  const auto off = span.offset;

  d_assert(getMetadataFlags(off) == 0 && getMetadataPtr(off) == 0);

  // now that we know they are available, set the empty pages to
  // in-use.  This is safe because this whole function is called
  // under the GlobalHeap lock, so there is no chance of concurrent
  // modification between the loop above and the one below.
  for (size_t i = 0; i < pageCount; i++) {
    d_assert(getMetadataFlags(off + i) == 0 && getMetadataPtr(off + i) == 0);
    setMetadata(off + i, internal::PageType::Identity);
  }

  void *ptr = ptrFromOffset(off);

  if (kAdviseDump) {
    madvise(ptr, sz, MADV_DODUMP);
  }

  return ptr;
}

void MeshableArena::free(void *ptr, size_t sz) {
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
    // clear the miniheap pointers we were tracking
    setMetadata(off + i, 0);
  }

  freeSpan(Span(off, pageCount));

  // debug("in use count after free of %p/%zu: %zu\n", ptr, sz, _bitmap.inUseCount());
}

void MeshableArena::scavenge() {
  // auto unmarkPages = [&] (const Span span) {
  //   for (size_t k = 0; k < span.length; k++) {
  //     bitmap.unset(span.offset + k);
  //   }
  // };
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
  mprotect(remove, sz, PROT_READ);
}

void MeshableArena::finalizeMesh(void *keep, void *remove, size_t sz) {
  // debug("keep: %p, remove: %p\n", keep, remove);
  const auto keepOff = offsetFor(keep);
  const auto removeOff = offsetFor(remove);
  d_assert(getMetadataFlags(keepOff) == internal::PageType::Identity);
  d_assert(getMetadataFlags(removeOff) != internal::PageType::Unallocated);

  const auto pageCount = sz / CPUInfo::PageSize;
  for (size_t i = 0; i < pageCount; i++) {
    setMetadata(removeOff + i, internal::PageType::Meshed | getMetadataPtr(keepOff));
  }

  void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, _fd, keepOff * CPUInfo::PageSize);
  hard_assert_msg(ptr != MAP_FAILED, "mesh remap failed: %d", errno);
  freePhys(remove, sz);

  mprotect(remove, sz, PROT_READ | PROT_WRITE);
}

#ifdef USE_MEMFD
static int sys_memfd_create(const char *name, unsigned int flags) {
  return syscall(__NR_memfd_create, name, flags);
}

int MeshableArena::openSpanFile(size_t sz) {
  errno = 0;
  int fd = sys_memfd_create("mesh_arena", MFD_CLOEXEC);
  d_assert_msg(fd >= 0, "memfd_create(%d) failed: %s", __NR_memfd_create, strerror(errno));

  int err = ftruncate(fd, sz);
  if (err != 0) {
    debug("ftruncate: %d\n", errno);
    abort();
  }

  return fd;
}
#else
int MeshableArena::openSpanFile(size_t sz) {
  constexpr size_t buf_len = 64;
  char buf[buf_len];
  memset(buf, 0, buf_len);

  d_assert(_spanDir != nullptr);
  sprintf(buf, "%s/XXXXXX", _spanDir);

  int fd = mkstemp(buf);
  if (fd < 0) {
    debug("mkstemp: %d\n", errno);
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
  runtime().lock();
  // runtime().heap().lock();

  int err = pipe(_forkPipe);
  if (err == -1)
    abort();
}

void MeshableArena::afterForkParent() {
  // debug("%d: after fork parent", getpid());
  // runtime().heap().unlock();
  runtime().unlock();

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
}

void MeshableArena::afterForkChild() {
  // debug("%d: after fork child", getpid());
  // runtime().heap().unlock();
  runtime().unlock();

  close(_forkPipe[0]);

  char *oldSpanDir = _spanDir;

  // update our pid + spanDir
  _spanDir = openSpanDir(getpid());
  d_assert(_spanDir != nullptr);

  // open new file for the arena
  int newFd = openSpanFile(kArenaSize);

  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(newFd, &fileinfo);
  d_assert(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == kArenaSize);

  const int oldFd = _fd;

  const auto bitmap = allocatedBitmap();
  for (auto const &i : bitmap) {
    int result = internal::copyFile(newFd, oldFd, i * CPUInfo::PageSize, CPUInfo::PageSize);
    d_assert(result == CPUInfo::PageSize);
  }

  // remap the new region over the old
  void *ptr = mmap(_arenaBegin, kArenaSize, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, newFd, 0);
  d_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  _fd = newFd;

  internal::Heap().free(oldSpanDir);

  while (write(_forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
  }
  close(_forkPipe[1]);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;
}
}  // namespace mesh
