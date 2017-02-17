// -*- mode: c++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure

  Copyright (C) 2000-2014 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#pragma once
#ifndef MESH__FILE_BACKED_MMAPHEAP_H
#define MESH__FILE_BACKED_MMAPHEAP_H

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

#include "internal.hh"

#include "mmapheap.hh"

/**
 * @class FileBackedMmapHeap
 * @brief Modified MmapHeap for use in Mesh.
 */

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

class FileBackedMmapHeap : public mesh::MmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(FileBackedMmapHeap);
  typedef MmapHeap SuperHeap;
  typedef int abstract_fd;

public:
  enum { Alignment = MmapWrapper::Alignment };

  explicit FileBackedMmapHeap();

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    int fd = openSpanFile(sz);
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

  static void mesh(FileBackedMmapHeap &heap, void *keep, void *remove) {
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
    // called
    rmdir(_spanDir);
  }

  void prepareForFork();
  void afterForkParent();
  void afterForkChild();

  internal::unordered_map<void *, shared_ptr<internal::FD>> _fdMap{};
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}

#endif  // MESH__FILE_BACKED_MMAPHEAP_H
