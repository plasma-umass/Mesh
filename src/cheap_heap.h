// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_CHEAP_HEAP_H
#define MESH_CHEAP_HEAP_H

#include "internal.h"
#include "one_way_mmap_heap.h"

namespace mesh {

// Fast allocation for a single size-class
template <size_t allocSize, size_t maxCount>
class CheapHeap : public OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(CheapHeap);
  typedef OneWayMmapHeap SuperHeap;

  static_assert(maxCount <= (1 << 30), "expected maxCount <= 2^30");
  static_assert(allocSize % 2 == 0, "expected allocSize to be even");

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

  inline void *alloc() {
    if (likely(_freelistOff >= 0)) {
      const auto ptr = _freelist[_freelistOff];
      _freelistOff--;
      return ptr;
    }

    const auto off = _arenaOff++;
    const auto ptr = ptrFromOffset(off);
    hard_assert(ptr < arenaEnd());
    return ptr;
  }

  constexpr size_t getSize(void *ATTRIBUTE_UNUSED ptr) const {
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

class DynCheapHeap : public OneWayMmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(DynCheapHeap);
  typedef OneWayMmapHeap SuperHeap;

public:
  // cacheline-sized alignment
  enum { Alignment = 64 };

  DynCheapHeap() : SuperHeap() {
    d_assert(_allocSize % 2 == 0);
  }

  inline void init(size_t allocSize, size_t maxCount, char *arena, void **freelist) {
    _arena = arena;
    _freelist = freelist;
    _allocSize = allocSize;
    _maxCount = maxCount;
    hard_assert(_arena != nullptr);
    hard_assert(_freelist != nullptr);
    hard_assert(reinterpret_cast<uintptr_t>(_arena) % Alignment == 0);
    hard_assert(reinterpret_cast<uintptr_t>(_freelist) % Alignment == 0);
  }

  inline void *alloc() {
    if (likely(_freelistOff >= 0)) {
      const auto ptr = _freelist[_freelistOff];
      _freelistOff--;
      return ptr;
    }

    const auto off = _arenaOff++;
    const auto ptr = ptrFromOffset(off);
    hard_assert(ptr < arenaEnd());
    return ptr;
  }

  size_t getSize(void *ATTRIBUTE_UNUSED ptr) const {
    return _allocSize;
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
    return (ptrval - arena) / _allocSize;
  }

  inline char *ptrFromOffset(size_t off) const {
    d_assert(off < _arenaOff);
    return _arena + off * _allocSize;
  }

  inline char *arenaEnd() const {
    return _arena + _allocSize * _maxCount;
  }

protected:
  char *_arena{nullptr};
  void **_freelist{nullptr};
  size_t _arenaOff{1};
  ssize_t _freelistOff{-1};
  size_t _allocSize{0};
  size_t _maxCount{0};
};
}  // namespace mesh

#endif  // MESH_CHEAP_HEAP_H
