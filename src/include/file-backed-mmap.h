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

#include "heaps/buildingblock/freelistheap.h"
#include "heaps/special/bumpalloc.h"
#include "heaps/special/zoneheap.h"
#include "heaps/threads/lockedheap.h"
#include "locks/posixlock.h"
#include "threads/cpuinfo.h"
#include "utility/myhashmap.h"
#include "utility/sassert.h"
#include "wrappers/mmapwrapper.h"
#include "wrappers/stlallocator.h"

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

namespace HL {

class PrivateFileBackedMmapHeap {
public:
  /// All memory from here is zeroed.
  enum { ZeroMemory = 1 };
  enum { Alignment = MmapWrapper::Alignment };

  static inline void *malloc(size_t sz) {
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
    if (fd < 0)
      abort();

    ftruncate(fd, sz);
    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED, fd, 0);

    if (ptr == MAP_FAILED)
      abort();
    return ptr;
  }

  static void free(void *ptr, size_t sz) {
    if ((long)sz < 0) {
      abort();
    }
    munmap(reinterpret_cast<char *>(ptr), sz);
  }
};

template <typename InternalAlloc = LockedHeap<PosixLockType, FreelistHeap<BumpAlloc<16384, PrivateFileBackedMmapHeap>>>>
class FileBackedMmapHeap : public PrivateFileBackedMmapHeap {
private:
  // Note: we never reclaim memory obtained for MyHeap, even when
  // this heap is destroyed.
  typedef MyHashMap<void *, size_t, InternalAlloc> mapType;

protected:
  mapType MyMap;
  PosixLockType MyMapLock;

public:
  enum { Alignment = PrivateFileBackedMmapHeap::Alignment };

  inline void *malloc(size_t sz) {
    void *ptr = PrivateFileBackedMmapHeap::malloc(sz);
    MyMapLock.lock();
    MyMap.set(ptr, sz);
    MyMapLock.unlock();
    assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);
    return const_cast<void *>(ptr);
  }

  inline size_t getSize(void *ptr) {
    MyMapLock.lock();
    size_t sz = MyMap.get(ptr);
    MyMapLock.unlock();
    return sz;
  }

  inline void free(void *ptr) {
    assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);
    MyMapLock.lock();
    size_t sz = MyMap.get(ptr);
    PrivateFileBackedMmapHeap::free(ptr, sz);
    MyMap.erase(ptr);
    MyMapLock.unlock();
  }
};
}

#endif  // HL_FILE_BACKED_MMAPHEAP_H
