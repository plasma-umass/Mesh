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
  // cacheline-sized alignment
  enum { Alignment = 64 };

  CheapHeap() : SuperHeap() {
    // TODO: check allocSize + maxCount doesn't overflow?
    _arena = reinterpret_cast<char *>(SuperHeap::malloc(allocSize * maxCount));
    _freelist = reinterpret_cast<void **>(SuperHeap::malloc(maxCount * sizeof(void *)));
    hard_assert(_arena != nullptr);
    hard_assert(_freelist != nullptr);
    d_assert(reinterpret_cast<uintptr_t>(_arena) % Alignment == 0);
    d_assert(reinterpret_cast<uintptr_t>(_freelist) % Alignment == 0);
  }

  CheapHeap(char *arena, char *freelist) : SuperHeap(), _arena{arena}, _freelist{freelist} {
    hard_assert(_arena != nullptr);
    hard_assert(_freelist != nullptr);
    d_assert(reinterpret_cast<uintptr_t>(_arena) % Alignment == 0);
    d_assert(reinterpret_cast<uintptr_t>(_freelist) % Alignment == 0);
  }

  inline void *alloc() {
    if (likely(_freelistOff >= 0)) {
      const auto ptr = _freelist[_freelistOff];
      _freelistOff--;
      return ptr;
    }

    const auto off = _arenaOff++;
    return ptrFromOffset(off);
  }

  constexpr size_t getSize(void *ptr) const {
    return allocSize;
  }

  inline void free(void *ptr) {
    d_assert(ptr >= _arena);
    d_assert(ptr < arenaEnd());

    _freelistOff++;
    _freelist[_freelistOff] = ptr;
  }

  inline char *arenaBegin() const {
    return _arena;
  }

  inline uint32_t offsetFor(const void *ptr) const {
    const uintptr_t ptrval = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t arena = reinterpret_cast<uintptr_t>(_arena);
    d_assert(ptrval >= arena);
    return (ptrval - arena) / allocSize;
  }

  inline char *ptrFromOffset(size_t off) const {
    d_assert(off < _arenaOff);
    return _arena + off * allocSize;
  }

  inline char *arenaEnd() const {
    return _arena + allocSize * maxCount;
  }

protected:
  char *_arena{nullptr};
  void **_freelist{nullptr};
  size_t _arenaOff{1};
  ssize_t _freelistOff{-1};
};
}  // namespace mesh

#endif  // MESH__CHEAP_HEAP_H
