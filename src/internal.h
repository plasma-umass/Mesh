// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__INTERNAL_H
#define MESH__INTERNAL_H

#include <atomic>
#include <unordered_set>

#include <signal.h>
#include <stdint.h>

#include "common.h"
#include "rng/mwc.h"

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "heaplayers.h"

#include "partitioned_heap.h"

namespace mesh {

namespace internal {
enum PageType {
  Clean = 0,
  Dirty = 1,
  Meshed = 2,
  Unknown = 3,
};
}  // namespace internal

class MiniHeapID {
public:
  MiniHeapID() noexcept : _id{0} {
  }

  explicit MiniHeapID(uint32_t id) : _id{id} {
  }

  MiniHeapID(const MiniHeapID &rhs) = default;

  constexpr MiniHeapID(MiniHeapID &&rhs) = default;

  MiniHeapID &operator=(const MiniHeapID &rhs) = default;

  bool operator==(const MiniHeapID &rhs) const {
    return _id == rhs._id;
  }

  bool hasValue() const {
    return _id != 0;
  }

  uint32_t value() const {
    return _id;
  }

private:
  uint32_t _id;
};

class MiniHeap;
MiniHeap *GetMiniHeap(const MiniHeapID id);
MiniHeapID GetMiniHeapID(const MiniHeap *mh);

typedef uint32_t Offset;
typedef uint32_t Length;

struct Span {
  // offset and length are in pages
  explicit Span(Offset _offset, Length _length) : offset(_offset), length(_length) {
  }

  Span(const Span &rhs) : offset(rhs.offset), length(rhs.length) {
  }

  constexpr Span &operator=(const Span &rhs) {
    offset = rhs.offset;
    length = rhs.length;
    return *this;
  }

  Span(Span &&rhs) : offset(rhs.offset), length(rhs.length) {
  }

  bool empty() const {
    return length == 0;
  }

  // reduce the size of this span to pageCount, return another span
  // with the rest of the pages.
  Span splitAfter(Length pageCount) {
    d_assert(pageCount <= length);
    auto restPageCount = length - pageCount;
    length = pageCount;
    return Span(offset + pageCount, restPageCount);
  }

  uint32_t spanClass() const {
    return std::min(length, kSpanClassCount) - 1;
  }

  size_t byteLength() const {
    return length * kPageSize;
  }

  inline bool operator==(const Span &rhs) {
    return offset == rhs.offset && length == rhs.length;
  }

  inline bool operator!=(const Span &rhs) {
    return !(*this == rhs);
  }

  Offset offset;
  Length length;
};

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

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<LockedHeap<PosixLockType, PartitionedHeap>> {
private:
  typedef ExactlyOneHeap<LockedHeap<PosixLockType, PartitionedHeap>> SuperHeap;

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

template <typename K>
using unordered_set = std::unordered_set<K, hash<K>, equal_to<K>, STLAllocator<K, Heap>>;

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

// based on LLVM's libcxx std::shuffle
template <class _RandomAccessIterator, class _RNG>
void mwcShuffle(_RandomAccessIterator __first, _RandomAccessIterator __last, _RNG &__rng) {
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;

  difference_type __d = __last - __first;
  if (__d > 1) {
    for (--__last, --__d; __first < __last; ++__first, --__d) {
      difference_type __i = __rng.inRange(0, __d);
      if (__i != difference_type(0))
        swap(*__first, *(__first + __i));
    }
  }
}

// ideally these would be static constants on BinToken, but that
// requires C++17
namespace bintoken {
static constexpr uint8_t BinMax = (1 << 3) - 1;
static constexpr uint32_t Max = (1 << 28) - 1;
static constexpr uint32_t MinFlags = Max - 1;
static constexpr uint8_t FlagFull = (1 << 3) - 2;
static constexpr uint8_t FlagEmpty = (1 << 3) - 3;
static constexpr uint32_t FlagNoOff = (1 << 28) - 1;

static_assert(FlagFull > kBinnedTrackerBinCount, "Full flag too small");
static_assert(FlagEmpty > kBinnedTrackerBinCount, "Full flag too small");
}  // namespace bintoken

// BinTokens are stored on MiniHeaps and used by the BinTracker to
// store occupancy metadata.  They are opaque to the MiniHeap, but
// storing the BinTracker's metadata on the MiniHeap saves a bunch of
// internal allocations and indirections.
class BinToken {
public:
  typedef uint8_t Bin;
  typedef uint32_t Size;

  BinToken() noexcept : _bin(bintoken::BinMax), _off(bintoken::Max) {
  }

  BinToken(Bin bin, Size off) noexcept : _bin(bin), _off(off) {
  }

  // whether this is a valid token, or just a default initialized one
  bool valid() const {
    return _bin < bintoken::BinMax && _off < bintoken::Max;
  }

  bool flagOk() const {
    return _off < bintoken::MinFlags;
  }

  static BinToken Full() {
    return BinToken{bintoken::FlagFull, bintoken::FlagNoOff};
  }

  static BinToken Empty() {
    return BinToken{bintoken::FlagEmpty, bintoken::FlagNoOff};
  }

  BinToken newOff(Size newOff) const {
    return BinToken{_bin, newOff};
  }

  Bin bin() const {
    return _bin;
  }

  Size off() const {
    return _off;
  }

private:
  Bin _bin : 4;
  Size _off : 28;
};

static_assert(sizeof(BinToken) == 4, "BinToken too big!");
}  // namespace internal
}  // namespace mesh

#endif  // MESH__INTERNAL_H
