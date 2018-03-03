// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__INTERNAL_H
#define MESH__INTERNAL_H

#include <atomic>

#include <signal.h>
#include <stdint.h>

#include "common.h"

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "heaplayers.h"

#include "bitmap.h"

namespace mesh {

static inline constexpr size_t PageCount(size_t sz) {
  return (sz + (HL::CPUInfo::PageSize - 1)) / HL::CPUInfo::PageSize;
}

static inline constexpr size_t RoundUpToPage(size_t sz) {
  return HL::CPUInfo::PageSize * PageCount(sz);
}

// keep in-sync with the version in plasma/mesh.h
enum BitType {
  MESH_BIT_0,
  MESH_BIT_1,
  MESH_BIT_2,
  MESH_BIT_3,
  MESH_BIT_COUNT,
};

namespace internal {

// return the kernel's perspective on our proportional set size
size_t measurePssKiB();

inline void *MaskToPage(const void *ptr) {
  const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
  return reinterpret_cast<void *>(ptrval & (uintptr_t) ~(CPUInfo::PageSize - 1));
}

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

typedef std::basic_string<char, std::char_traits<char>, STLAllocator<char, Heap>> string;

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

// BinTokens are stored on MiniHeaps and used by the BinTracker to
// store occupancy metadata.  They are opaque to the MiniHeap, but
// storing the BinTracker's metadata on the MiniHeap saves a bunch of
// internal allocations and indirections.
class BinToken {
public:
  typedef uint32_t Size;
  // C++ 17 has inline vars, but earlier versions don't
  static constexpr inline Size Max = numeric_limits<uint32_t>::max();
  static constexpr inline Size MinFlags = numeric_limits<uint32_t>::max() - 4;
  static constexpr inline Size FlagFull = numeric_limits<uint32_t>::max() - 1;
  static constexpr inline Size FlagEmpty = numeric_limits<uint32_t>::max() - 2;
  static constexpr inline Size FlagNoOff = numeric_limits<uint32_t>::max();

  BinToken() : _bin(Max), _off(Max) {
  }

  BinToken(Size bin, Size off) : _bin(bin), _off(off) {
  }

  // whether this is a valid token, or just a default initialized one
  bool valid() const {
    return _bin < Max && _off < Max;
  }

  bool flagOk() const {
    return _off < MinFlags;
  }

  static BinToken Full() {
    return BinToken{FlagFull, FlagNoOff};
  }

  static BinToken Empty() {
    return BinToken{FlagEmpty, FlagNoOff};
  }

  BinToken newOff(Size newOff) const {
    return BinToken{_bin, newOff};
  }

  Size bin() const {
    return _bin;
  }

  Size off() const {
    return _off;
  }

private:
  Size _bin;
  Size _off;
};

static_assert(sizeof(BinToken) == 8, "BinToken too big!");

}  // namespace internal
}  // namespace mesh

#endif  // MESH__INTERNAL_H
