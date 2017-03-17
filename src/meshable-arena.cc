// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#define USE_MEMFD 1

#ifdef USE_MEMFD
#include <sys/syscall.h>

#include <unistd.h>

//#include <sys/memfd.h>
#include <asm/unistd_64.h>
#include <linux/memfd.h>
#endif

#include <linux/fs.h>
#include <sys/ioctl.h>

#include "meshable-arena.h"

#include "runtime.h"

namespace mesh {

static void *arenaInstance;

static const char *const TMP_DIRS[] = {
    "/dev/shm", "/tmp",
};

MeshableArena::MeshableArena() : SuperHeap(), _bitmap{internal::ArenaSize / CPUInfo::PageSize} {
  d_assert(arenaInstance == nullptr);
  arenaInstance = this;

#ifndef USE_MEMFD
  _spanDir = openSpanDir(getpid());
  d_assert(_spanDir != nullptr);
#endif

  int fd = openSpanFile(internal::ArenaSize);
  if (fd < 0) {
    debug("mesh: opening arena file failed.\n");
    abort();
  }
  _fd = internal::make_shared<internal::FD>(fd);
  _arenaBegin = SuperHeap::map(internal::ArenaSize, MAP_SHARED, fd);
  d_assert(_arenaBegin != nullptr);

  // debug("MeshableArena(%p): fd:%4d\t%p-%p\n", this, fd, _arenaBegin, arenaEnd());

  // TODO: move this to runtime
  on_exit(staticOnExit, this);
  pthread_atfork(staticPrepareForFork, staticAfterForkParent, staticAfterForkChild);
}

void MeshableArena::internalMesh(void *keep, void *remove) {
  d_assert_msg(false, "TODO: internal mesh", "");

  // auto sz = _vmaMap[keep];

  // d_assert(_fdMap.find(keep) != _fdMap.end());
  // auto keepFd = _fdMap[keep];
  // void *ptr = mmap(remove, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, *keepFd, 0);
  // d_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  // _fdMap[remove] = keepFd;

  debug("meshed %p + %p", keep, remove);
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

void MeshableArena::staticOnExit(int code, void *data) {
  reinterpret_cast<MeshableArena *>(data)->exit();
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
  runtime().lock();
  runtime().heap().lock();

  int err = pipe(_forkPipe);
  if (err == -1)
    abort();
}

void MeshableArena::afterForkParent() {
  runtime().heap().unlock();
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
  runtime().heap().unlock();
  runtime().unlock();

  close(_forkPipe[0]);

  char *oldSpanDir = _spanDir;

  // update our pid + spanDir
  _spanDir = openSpanDir(getpid());
  d_assert(_spanDir != nullptr);

  // open new file for the arena
  int newFd = openSpanFile(internal::ArenaSize);

  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(newFd, &fileinfo);
  d_assert(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == internal::ArenaSize);

  const int oldFd = *_fd;

  const size_t arenaPages = _bitmap.bitCount();
  for (size_t i = 0; i < arenaPages; ++i) {
    if (!_bitmap.isSet(i))
      continue;

    int result = internal::copyFile(newFd, oldFd, i * CPUInfo::PageSize, CPUInfo::PageSize);
    d_assert(result == CPUInfo::PageSize);
  }

  // remap the new region over the old
  void *ptr = mmap(_arenaBegin, internal::ArenaSize, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, newFd, 0);
  d_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  _fd = internal::make_shared<internal::FD>(newFd);

  internal::Heap().free(oldSpanDir);

  while (write(_forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
  }
  close(_forkPipe[1]);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;
}
}
