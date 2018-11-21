// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
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
#ifndef MESH__MMAP_HEAP_H
#define MESH__MMAP_HEAP_H

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

#include "internal.h"
#include "one_way_mmap_heap.h"

namespace mesh {

// MmapHeap extends OneWayMmapHeap to track allocated address space
// and will free memory with calls to munmap.
class MmapHeap : public OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MmapHeap);
  typedef OneWayMmapHeap SuperHeap;

public:
  enum { Alignment = MmapWrapper::Alignment };

  MmapHeap() : SuperHeap() {
  }

  inline void *malloc(size_t sz) {
    auto ptr = map(sz, MAP_PRIVATE | MAP_ANONYMOUS, -1);

    d_assert(_vmaMap.find(ptr) == _vmaMap.end());
    _vmaMap[ptr] = sz;
    d_assert(_vmaMap.find(ptr) != _vmaMap.end());
    d_assert(_vmaMap[ptr] == sz);

    return ptr;
  }

  inline size_t getSize(void *ptr) const {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("mmap: invalid getSize: %p", ptr);
      abort();
      return 0;
    }
    return entry->second;
  }

  inline bool inBounds(void *ptr) const {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      return false;
    }
    // FIXME: this isn't right -- we want inclusion not exact match
    return true;
  }

  inline void free(void *ptr) {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("mmap: invalid free, possibly from memalign: %p", ptr);
      // abort();
      return;
    }

    auto sz = entry->second;

    munmap(ptr, sz);
    // madvise(ptr, sz, MADV_DONTNEED);
    // mprotect(ptr, sz, PROT_NONE);

    _vmaMap.erase(entry);
    d_assert(_vmaMap.find(ptr) == _vmaMap.end());
  }

  // return the sum of the sizes of all large allocations
  size_t arenaSize() const {
    size_t sz = 0;
    for (auto it = _vmaMap.begin(); it != _vmaMap.end(); it++) {
      sz += it->second;
    }
    return sz;
  }

protected:
  internal::unordered_map<void *, size_t> _vmaMap{};
};
}  // namespace mesh

#endif  // MESH__MESH_MMAP_H
