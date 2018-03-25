// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2018 Bobby Powers

#pragma once
#ifndef MESH__CHEAP_HEAP_H
#define MESH__CHEAP_HEAP_H

#include "internal.h"

#include "mmapheap.h"

namespace mesh {

// Fast allocation for a single size-class
template <size_t allocSize, size_t maxCount>
class CheapHeap : public OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(CheapHeap);
  typedef OneWayMmapHeap SuperHeap;

  static_assert(allocSize % 2 == 0);

public:
  enum { Alignment = MmapWrapper::Alignment };

  CheapHeap() : SuperHeap() {
  }

  inline void *malloc(size_t sz) {

    return ptr;
  }

  constexpr size_t getSize(void *ptr) const {
    return allocSize;
  }

  inline void free(void *ptr) {
    d_assert(ptr >= _arena);
    d_assert(ptr < arenaEnd());
  }

  inline char *arenaEnd() const {
    return _arena + allocSize * maxCount;
  }

protected:
  char *_arena{nullptr};
  void *_freelist{nullptr};
  size_t _arenaOff{0};
  size_t _freelistOff{0};
};
}  // namespace mesh

#endif  // MESH__CHEAP_HEAP_H
