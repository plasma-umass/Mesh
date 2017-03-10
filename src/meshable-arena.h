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
#include <map>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <copyfile.h>
#else
#include <sys/sendfile.h>
#endif

#include <new>

#include "internal.h"

#include "mmapheap.h"

namespace mesh {

namespace internal {
// wraps an integer file descriptor to provide close-on-destruct
// semantics (so that we can use a shared_ptr to refcount a FD)
class FD {
private:
  DISALLOW_COPY_AND_ASSIGN(FD);

public:
  explicit FD(int fd) : _fd{fd} {
  }

  ~FD() {
    if (_fd >= 0)
      close(_fd);
    _fd = -2;
  }

  operator int() const {
    return _fd;
  }

protected:
  int _fd;
};
}

class MeshableArena : public mesh::MmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshableArena);
  typedef MmapHeap SuperHeap;

public:
  enum { Alignment = MmapWrapper::Alignment };

  explicit MeshableArena();

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    d_assert_msg(sz % HL::CPUInfo::PageSize == 0, "multiple-page allocs only, sz bad: %zu", sz);

    if (sz == HL::CPUInfo::PageSize) {
      debug("TODO: Arena: search bitmap for first 0");
      return 0;
    }

    debug("TODO: Arena: otherwise allocate after last set bit for now, I guess");
    return 0;
  }

  inline void free(void *ptr) {
    // TODO: munmap, punch hole, free bitmap

    // munmap(ptr, sz);
    // madvise(ptr, sz, MADV_DONTNEED);
    // mprotect(ptr, sz, PROT_NONE);

    // int fd = *_fd;
    // int result = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, sz);
    // d_assert(result == 0);
  }

  // must be called with the world stopped
  static void mesh(MeshableArena &heap, void *keep, void *remove) {
    heap.internalMesh(keep, remove);
  }

  void internalMesh(void *keep, void *remove);

private:
  int openSpanFile(size_t sz);
  char *openSpanDir(int pid);

  static void staticOnExit(int code, void *data);
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

  void *arenaEnd() {
    return reinterpret_cast<char *>(_arenaBegin) + internal::ArenaSize;
  }

  void *_arenaBegin{nullptr};

  internal::Bitmap _bitmap;
  shared_ptr<internal::FD> _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}

#endif  // MESH__MESHABLE_ARENA_H
