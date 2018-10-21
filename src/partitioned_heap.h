// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#pragma once
#ifndef MESH__PARTITIONED_HEAP_H
#define MESH__PARTITIONED_HEAP_H

// we can only include "common.h", as the partitioned heap is used as
// the allocator-internal heap in "internal.h"
#include "common.h"

#include "one_way_mmap_heap.h"

namespace mesh {

static constexpr int kPartitionedHeapNBins = 10;
static constexpr int kPartitionedHeapArenaSize = 256 * 1024 * 1024;  // 256 MB

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

    // TODO: calc freelist + arena offsets
  }

  inline void *malloc(size_t sz) {
    const auto sizeClass = powerOfTwo::ClassForByteSize(sz);

    if (unlikely(sizeClass >= kPartitionedHeapNBins)) {
      return _bigHeap.malloc(sz);
    }

    const auto sizeClass = getSizeClass(ptr);

    if (likely(_freelistOff >= 0)) {
      const auto ptr = _freelist[_freelistOff];
      _freelistOff--;
      return ptr;
    }

    const auto off = _arenaOff++;
    return ptrFromOffset(off, sizeClass);
  }

  inline void free(void *ptr) {
    if (unlikely(!contains(ptr))) {
      _bigHeap.free(ptr);
      return;
    }

    const auto sizeClass = getSizeClass(ptr);
    d_assert(sizeClass >= 0);
    d_assert(sizeClass < kPartitionedHeapNBins);

    _freelistOff[sizeClass]++ _freelist[sizeClass][_freelistOff[sizeClass]] = ptr;
  }

  inline size_t getSize(void *ptr) {
    if (unlikely(!contains(ptr))) {
      return _bigHeap.getSize(ptr);
    }

    return powerOfTwo::ByteSizeForClass(getSizeClass(ptr));
  }

  inline int getSizeClass(void *ptr) const {
    // TODO
    return -1;
  }

  inline bool contains(void *ptr) const {
    const auto ptrval = reinterpret_cast<char *>(ptr);
    return ptrval >= _smallArena && ptrval < _smallArenaEnd;
  }

  inline char *ptrFromOffset(size_t off, int sizeClass) const {
    return _smallArena + off * powerOfTwo::ByteSizeForClass(sizeClass);
  }

private:
  char *_smallArena{nullptr};
  char *_smallArenaEnd{nullptr};
  char *_arena[kPartitionedHeapNBins]{nullptr};
  void **_freelist[kPartitionedHeapNBins]{nullptr};
  size_t _arenaOff[kPartitionedHeapNBins]{1};
  ssize_t _freelistOff[kPartitionedHeapNBins]{-1};
  HL::MmapHeap _bigHeap{};
};
}  // namespace mesh

#endif  // MESH__PARTITIONED_HEAP_H
