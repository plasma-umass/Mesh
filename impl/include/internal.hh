// -*- mode: c++ -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__INTERNAL_H
#define MESH__INTERNAL_H

#include "common.hh"

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "heaplayers.h"

namespace mesh {
namespace internal {

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<LockedHeap<PosixLockType, DebugHeap<SizeHeap<BumpAlloc<16384 * 8, MmapHeap, 16>>>>> {
protected:
  typedef ExactlyOneHeap<LockedHeap<PosixLockType, DebugHeap<SizeHeap<BumpAlloc<16384 * 8, MmapHeap, 16>>>>> SuperHeap;

public:
  Heap() : SuperHeap() {
    static_assert(Alignment % 16 == 0, "16-byte alignment");
  }

  inline void *malloc(size_t sz) {
    auto ptr = SuperHeap::malloc(sz);
    d_assert(reinterpret_cast<uint64_t>(ptr) % 16 == 0);
    return ptr;
  }
};

// make a shared pointer allocated from our internal heap that will
// also free to the internal heap when all references have been
// dropped.
template <typename T, class... Args>
inline std::shared_ptr<T> make_shared(Args &&... args) {
  // FIXME: somehow centralize this static.
  static STLAllocator<T, Heap> heap;
  return std::allocate_shared<T, STLAllocator<T, Heap>, Args...>(heap, std::forward<Args>(args)...);
}

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V, hash<K>, equal_to<K>, STLAllocator<pair<const K, V>, Heap>>;

template <typename K, typename V>
using map = std::map<K, V, std::less<K>, STLAllocator<pair<const K, V>, Heap>>;

template <typename T>
using vector = std::vector<T, STLAllocator<T, Heap>>;

// https://stackoverflow.com/questions/529831/returning-the-greatest-key-strictly-less-than-the-given-key-in-a-c-map
template <typename Map>
typename Map::const_iterator greatest_leq(Map const &m, typename Map::key_type const &k) {
  typename Map::const_iterator it = m.upper_bound(k);
  if (it != m.begin()) {
    return --it;
  }
  return m.end();
}

// https://stackoverflow.com/questions/529831/returning-the-greatest-key-strictly-less-than-the-given-key-in-a-c-map
template <typename Map>
typename Map::iterator greatest_leq(Map &m, typename Map::key_type const &k) {
  typename Map::iterator it = m.upper_bound(k);
  if (it != m.begin()) {
    return --it;
  }
  return m.end();
}
}
}

#endif  // MESH__INTERNAL_H
