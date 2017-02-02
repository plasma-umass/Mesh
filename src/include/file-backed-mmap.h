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

#ifndef HL_FILE_BACKED_MMAPHEAP_H
#define HL_FILE_BACKED_MMAPHEAP_H

#if defined(_WIN32)
#error "TODO"
#include <windows.h>
#else
// UNIX
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#endif

#include <new>

#include "internal.h"

#ifndef HL_MMAP_PROTECTION_MASK
#error "define HL_MMAP_PROTECTION_MASK before including heaplayers.h"
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

/**
 * @class FileBackedMmapHeap
 * @brief Modified MmapHeap for use in Mesh.
 */

namespace mesh {

class FileBackedMmapHeap {

protected:
  internal::unordered_map<void *, size_t> vmaMap;
  internal::unordered_map<void *, int> fdMap;
  std::mutex mapLock;

public:
  enum { Alignment = MmapWrapper::Alignment };

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    char buf[64];
    memset(buf, 0, 64);
    sprintf(buf, "/tmp/alloc-mesh-%d", getpid());
    mkdir(buf, 0755);
    sprintf(buf, "/tmp/alloc-mesh-%d/XXXXXX", getpid());

    int fd = mkstemp(buf);
    if (fd < 0) {
      debug("mkstemp: %d\n", errno);
      abort();
    }

    int err = ftruncate(fd, sz);
    if (err != 0) {
      debug("ftruncate: %d\n", errno);
      abort();
    }

    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED, fd, 0);

    if (ptr == MAP_FAILED)
      abort();

    {
      std::lock_guard<std::mutex> lock(mapLock);
      vmaMap[ptr] = sz;
      fdMap[ptr] = fd;
    }

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);
    return const_cast<void *>(ptr);
  }

  inline size_t getSize(void *ptr) {
    std::lock_guard<std::mutex> lock(mapLock);

    auto itVma = vmaMap.find(ptr);
    if (itVma == vmaMap.end())
      return 0;
    return itVma->second;
  }

  inline void free(void *ptr) {
    std::lock_guard<std::mutex> lock(mapLock);
    auto itVma = vmaMap.find(ptr);
    if (itVma == vmaMap.end())
      return;

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    auto itFd = fdMap.find(ptr);
    d_assert(itFd != fdMap.end());

    //munmap(ptr, itVma->second);
    // TODO: unlink
    close(itFd->second);

    vmaMap.erase(itVma);
    fdMap.erase(itFd);

    debug("FileBackedMmap: freed %p", ptr);
  }
};

class MmapHeap {

protected:
  internal::unordered_map<void *, size_t> vmaMap;
  std::mutex mapLock;

public:
  enum { Alignment = MmapWrapper::Alignment };

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
      abort();

    {
      std::lock_guard<std::mutex> lock(mapLock);
      vmaMap[ptr] = sz;
    }

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);
    return const_cast<void *>(ptr);
  }

  inline size_t getSize(void *ptr) {
    std::lock_guard<std::mutex> lock(mapLock);

    auto itVma = vmaMap.find(ptr);
    if (itVma == vmaMap.end())
      return 0;
    return itVma->second;
  }

  inline void free(void *ptr) {
    std::lock_guard<std::mutex> lock(mapLock);
    auto itVma = vmaMap.find(ptr);
    if (itVma == vmaMap.end())
      return;

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    vmaMap.erase(itVma);
  }
};
}

#endif  // HL_FILE_BACKED_MMAPHEAP_H
