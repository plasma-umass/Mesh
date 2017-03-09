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

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    void *ptr = SuperHeap::map(sz, MAP_SHARED, fd);

    _fdMap[ptr] = internal::make_shared<internal::FD>(fd);

    return ptr;
  }

  inline void free(void *ptr) {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("fb-mmap: invalid free: %p", ptr);
      return;
    }

    auto sz = entry->second;

    munmap(ptr, sz);
    // madvise(ptr, sz, MADV_DONTNEED);
    // mprotect(ptr, sz, PROT_NONE);

    _vmaMap.erase(entry);
    d_assert(_vmaMap.find(ptr) == _vmaMap.end());

    d_assert(_fdMap.find(ptr) != _fdMap.end());
    int fd = *_fdMap[ptr];
    int result = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, sz);
    d_assert(result == 0);

    _fdMap.erase(ptr);
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

  shared_ptr<internal::FD> _fd;
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}

#endif  // MESH__MESHABLE_ARENA_H
