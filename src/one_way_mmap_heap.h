// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Heap-Layers and Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_ONE_WAY_MMAP_HEAP_H
#define MESH_ONE_WAY_MMAP_HEAP_H

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
// UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "common.h"

#if defined(_WIN32)
// Windows doesn't have these mmap flags - define them for API compatibility
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x0001
#endif
#ifndef MAP_SHARED
#define MAP_SHARED 0x0002
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x0004
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0x0008
#endif
#ifndef MAP_FIXED
#define MAP_FIXED 0x0010
#endif
#else
// Unix
#ifndef HL_MMAP_PROTECTION_MASK
#error "define HL_MMAP_PROTECTION_MASK before including mmapheap.h"
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
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

#if defined(_WIN32)
  // Windows implementation using VirtualAlloc for anonymous mappings
  // and MapViewOfFile for file-backed mappings.
  inline void *map(size_t sz, int flags, HANDLE fd = INVALID_HANDLE_VALUE) {
    if (sz == 0)
      return nullptr;

    // Round up to allocation granularity (64KB on Windows).
    // Windows requires VirtualAlloc addresses to be aligned to allocation granularity.
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    const size_t granularity = sysInfo.dwAllocationGranularity;
    sz = (sz + granularity - 1) & ~(granularity - 1);

    void *ptr = nullptr;

    if (fd == INVALID_HANDLE_VALUE || (flags & MAP_ANONYMOUS)) {
      // Anonymous mapping - use VirtualAlloc
      ptr = VirtualAlloc(nullptr, sz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    } else {
      // File-backed mapping - use MapViewOfFile
      ptr = MapViewOfFile(fd, FILE_MAP_ALL_ACCESS, 0, 0, sz);
    }

    if (ptr == nullptr)
      abort();

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    return ptr;
  }

  inline void *malloc(size_t sz) {
    return map(sz, MAP_PRIVATE | MAP_ANONYMOUS, INVALID_HANDLE_VALUE);
  }
#else
  // Unix implementation using mmap
  inline void *map(size_t sz, int flags, int fd = -1) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    const size_t pageSize = getPageSize();
    sz = (sz + pageSize - 1) & (size_t)~(pageSize - 1);

    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, flags, fd, 0);
    if (ptr == MAP_FAILED)
      abort();

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    return ptr;
  }

  inline void *malloc(size_t sz) {
    return map(sz, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1);
  }
#endif

  inline size_t getSize(void *ATTRIBUTE_UNUSED ptr) const {
    return 0;
  }

  inline void free(void *ATTRIBUTE_UNUSED ptr) {
  }
};

}  // namespace mesh

#endif  // MESH_ONE_WAY_MMAP_HEAP_H
