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
#ifndef MESH__MESH_ONE_WAY_MMAP_HEAP_H
#define MESH__MESH_ONE_WAY_MMAP_HEAP_H

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

#include "common.h"

#ifndef HL_MMAP_PROTECTION_MASK
#error "define HL_MMAP_PROTECTION_MASK before including mmapheap.h"
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace mesh {

// OneWayMmapHeap allocates address space through calls to mmap and
// will never unmap address space.
class OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(OneWayMmapHeap);

public:
  enum { Alignment = MmapWrapper::Alignment };

  OneWayMmapHeap() {
  }

  inline void *map(size_t sz, int flags, int fd = -1) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + kPageSize - 1) & (size_t) ~(kPageSize - 1);

    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, flags, fd, 0);
    if (ptr == MAP_FAILED)
      abort();

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    return ptr;
  }

  inline void *malloc(size_t sz) {
    return map(sz, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1);
  }

  inline size_t getSize(void *ptr) const {
    return 0;
  }

  inline void free(void *ptr) {
  }
};

}  // namespace mesh

#endif  // MESH__MESH_ONE_WAY_MMAP_HEAP_H
