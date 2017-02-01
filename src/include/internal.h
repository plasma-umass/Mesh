// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_INTERNAL_H
#define MESH_INTERNAL_H

#include <unordered_map>
#include <cstddef>

#include "heaplayers.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void debug(const char* fmt, ...);

namespace mesh {
namespace internal {
using namespace HL;

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<LockedHeap<PosixLockType, BumpAlloc<16384 * 8, MmapHeap, 16>>> {};

template <typename T>
struct StlAllocator {
  typedef T value_type;
  T* allocate(std::size_t n) {
    return Heap().malloc(n);
  }
  void deallocate(T* p, std::size_t n) {
    Heap().free(p);
  }
};

template <class T, class U>
bool operator==(const StlAllocator<T>&, const StlAllocator<U>&);
template <class T, class U>
bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&);

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V, hash<K>, equal_to<K>, StlAllocator<pair<const K, V>>>;
}
}

#endif  // MESH_INTERNAL_H
