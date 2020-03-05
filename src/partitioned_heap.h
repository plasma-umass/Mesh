// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_PARTITIONED_HEAP_H
#define MESH_PARTITIONED_HEAP_H

// we can only include "common.h", as the partitioned heap is used as
// the allocator-internal heap in "internal.h"
#include "common.h"

#include "cheap_heap.h"
#include "one_way_mmap_heap.h"

namespace mesh {

static constexpr int kPartitionedHeapNBins = 16;
static constexpr int kPartitionedHeapArenaSize = 512 * 1024 * 1024;  // 512 MB
static constexpr int kPartitionedHeapSizePer = kPartitionedHeapArenaSize / kPartitionedHeapNBins;

// Fast allocation for multiple size classes
class PartitionedHeap : public OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(PartitionedHeap);
  typedef OneWayMmapHeap SuperHeap;

public:
  PartitionedHeap() : SuperHeap() {
    _smallArena = reinterpret_cast<char *>(SuperHeap::malloc(kPartitionedHeapArenaSize));
    hard_assert(_smallArena != nullptr);
    _smallArenaEnd = _smallArena + kPartitionedHeapArenaSize;

    auto freelist = reinterpret_cast<char *>(SuperHeap::malloc(kPartitionedHeapArenaSize));
    hard_assert(freelist != nullptr);

    for (size_t i = 0; i < kPartitionedHeapNBins; ++i) {
      auto arenaStart = _smallArena + i * kPartitionedHeapSizePer;
      auto freelistStart = freelist + i * kPartitionedHeapSizePer;

      const auto allocSize = powerOfTwo::ByteSizeForClass(i);
      const auto maxCount = kPartitionedHeapSizePer / allocSize;

      // mesh::debug("internal heap of allocSize %zu\n", allocSize);

      _smallHeaps[i].init(allocSize, maxCount, arenaStart, reinterpret_cast<void **>(freelistStart));
    }

    // TODO: calc freelist + arena offsets
  }

  inline void *malloc(size_t sz) {
    const auto sizeClass = powerOfTwo::ClassForByteSize(sz);

    if (unlikely(sizeClass >= kPartitionedHeapNBins)) {
      auto res = _bigHeap.malloc(sz);
      // debug("internalHeap::malloc(%zu): %p (big)\n", sz, res);
      return res;
    }

    auto res = _smallHeaps[sizeClass].alloc();
    // debug("internalHeap::malloc(%zu): %p\n", sz, res);
    return res;
  }

  inline void free(void *ptr) {
    // debug("internalHeap::free(): %p\n", ptr);
    if (unlikely(!contains(ptr))) {
      _bigHeap.free(ptr);
      return;
    }

    const auto sizeClass = getSizeClass(ptr);
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < kPartitionedHeapNBins);

    return _smallHeaps[sizeClass].free(ptr);
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(!contains(ptr))) {
      return _bigHeap.getSize(ptr);
    }

    return powerOfTwo::ByteSizeForClass(getSizeClass(ptr));
  }

  // must be protected with a contains()
  inline int getSizeClass(void *ptr) const {
    const char *ptrval = reinterpret_cast<char *>(ptr);
    const auto sizeClass = (ptrval - _smallArena) / kPartitionedHeapSizePer;
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < kPartitionedHeapNBins);
    return sizeClass;
  }

  inline bool contains(void *ptr) const {
    const auto ptrval = reinterpret_cast<char *>(ptr);
    return ptrval >= _smallArena && ptrval < _smallArenaEnd;
  }

private:
  char *_smallArena{nullptr};
  char *_smallArenaEnd{nullptr};
  DynCheapHeap _smallHeaps[kPartitionedHeapNBins]{};
  HL::MmapHeap _bigHeap{};
};
}  // namespace mesh

#endif  // MESH_PARTITIONED_HEAP_H
