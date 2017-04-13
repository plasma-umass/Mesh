// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__INTERNAL_H
#define MESH__INTERNAL_H

#include <signal.h>

#include "common.h"

#include "bitmap.h"

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "heaplayers.h"

namespace mesh {

static inline size_t PageCount(size_t sz) {
  return (sz + (HL::CPUInfo::PageSize - 1)) / HL::CPUInfo::PageSize;
}

static inline size_t RoundUpToPage(size_t sz) {
  return HL::CPUInfo::PageSize * PageCount(sz);
}

namespace internal {

static constexpr bool SlowButAccurateRandom = false;
static constexpr size_t MeshMarker = 7305126540297948313;
static inline bool isMeshMarker(void *ptr) {
  return reinterpret_cast<size_t>(ptr) == internal::MeshMarker;
}

// static constexpr size_t ArenaSize = 1UL << 35;        // 32 GB
static constexpr size_t ArenaSize = 1UL << 30;        // 32 GB
static constexpr size_t ALTSTACK_SIZE = 16 * 1024UL;  // 16k sigaltstacks
#define SIGQUIESCE (SIGRTMIN + 7)

// efficiently copy data from srcFd to dstFd
int copyFile(int dstFd, int srcFd, off_t off, size_t sz);

class InternalTopHeap : public SizeHeap<BumpAlloc<16384 * 8, MmapHeap, 16>> {
private:
  typedef SizeHeap<BumpAlloc<16384 * 8, MmapHeap, 16>> SuperHeap;

public:
  // inline void *malloc(size_t sz) {
  //   debug("internal::Heap(%p)::malloc(%zu)\n", this, sz);
  //   return SuperHeap::malloc(sz);
  // }
};

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<
                 LockedHeap<PosixLockType, DebugHeap<KingsleyHeap<FreelistHeap<InternalTopHeap>, MmapHeap>>>> {
protected:
  typedef ExactlyOneHeap<LockedHeap<PosixLockType, DebugHeap<KingsleyHeap<FreelistHeap<InternalTopHeap>, MmapHeap>>>>
      SuperHeap;

public:
  Heap() : SuperHeap() {
    static_assert(Alignment % 16 == 0, "16-byte alignment");
  }

  // inline void *malloc(size_t sz) {
  //   debug("internal::Heap(%p)::malloc(%zu)\n", this, sz);
  //   return SuperHeap::malloc(sz);
  // }
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

extern STLAllocator<char, Heap> allocator;

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

typedef Bitmap<Heap> Bitmap;

}  // namespace internal
}  // namespace mesh

#endif  // MESH__INTERNAL_H
