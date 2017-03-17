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

    d_assert(_arenaBegin != nullptr);
    d_assert_msg(sz % HL::CPUInfo::PageSize == 0, "multiple-page allocs only, sz bad: %zu", sz);

    if (sz == HL::CPUInfo::PageSize) {
      size_t page = _bitmap.setFirstEmpty();
      return reinterpret_cast<char *>(_arenaBegin) + HL::CPUInfo::PageSize * page;
    }

    const auto nPages = sz / HL::CPUInfo::PageSize;
    d_assert(nPages >= 2);

    // FIXME: we could be smarter here
    size_t firstPage = 0;
    bool found = false;
    while (!found) {
      firstPage = _bitmap.setFirstEmpty(firstPage);
      found = true;
      for (size_t i = 1; i < nPages; i++) {
        // if one of the pages we need is already in use, bump our
        // offset into the page index up and try again
        if (_bitmap.isSet(firstPage + i)) {
          firstPage += i + 1;
          found = false;
          break;
        }
      }
    }

    for (size_t i = 1; i < nPages; i++) {
      bool ok = _bitmap.tryToSet(firstPage + i);
      d_assert(ok);
    }

    return reinterpret_cast<char *>(_arenaBegin) + HL::CPUInfo::PageSize * firstPage;
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

  // per-page bitmap
  internal::Bitmap _bitmap;

  shared_ptr<internal::FD> _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}

#endif  // MESH__MESHABLE_ARENA_H
