// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Heap-Layers and Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_MMAP_HEAP_H
#define MESH_MMAP_HEAP_H

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

#endif  // MESH_MESH_MMAP_H
