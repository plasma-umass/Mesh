// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_INTERNAL_H
#define MESH_INTERNAL_H

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <atomic>
#include <unordered_set>

#include <signal.h>
#include <stdint.h>

#include "common.h"
#include "rng/mwc.h"

// never allocate executable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)
#define MALLOC_TRACE 0

#include "heaplayers.h"

#include "partitioned_heap.h"

#ifdef __linux__
inline pid_t gettid(void) {
  return syscall(__NR_gettid);
}
#endif
#ifdef __APPLE__
inline pid_t gettid(void) {
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  return static_cast<uint32_t>(tid);
}
#endif

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

  explicit constexpr MiniHeapID(uint32_t id) : _id{id} {
  }

  MiniHeapID(const MiniHeapID &rhs) = default;

  constexpr MiniHeapID(MiniHeapID &&rhs) = default;

  MiniHeapID &operator=(const MiniHeapID &rhs) = default;

  bool operator!=(const MiniHeapID &rhs) const {
    return _id != rhs._id;
  }

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

namespace list {
static constexpr MiniHeapID Head{UINT32_MAX};
// TODO: add a type to represent this
static constexpr uint8_t Full = 0;
static constexpr uint8_t Partial = 1;
static constexpr uint8_t Empty = 2;
static constexpr uint8_t Attached = 3;
static constexpr uint8_t Max = 4;
}  // namespace list

class MiniHeap;
MiniHeap *GetMiniHeap(const MiniHeapID id);
MiniHeapID GetMiniHeapID(const MiniHeap *mh);

typedef std::array<MiniHeap *, kMaxSplitListSize> SplitArray;
typedef std::array<std::pair<MiniHeap *, MiniHeap *>, kMaxMergeSets> MergeSetArray;

template <typename Object, typename ID>
class ListEntry {
public:
  typedef ListEntry<Object, ID> Entry;

  ListEntry() noexcept : _prev{}, _next{} {
  }

  explicit constexpr ListEntry(ID prev, ID next) : _prev{prev}, _next{next} {
  }

  ListEntry(const ListEntry &rhs) = default;
  ListEntry &operator=(const ListEntry &) = default;

  inline bool empty() const {
    return !_prev.hasValue() || !_next.hasValue();
  }

  inline ID next() const {
    return _next;
  }

  inline ID prev() const {
    return _prev;
  }

  inline void setNext(ID next) {
    // mesh::debug("%p.setNext: %u\n", this, next);
    _next = next;
  }

  inline void setPrev(ID prev) {
    _prev = prev;
  }

  // add calls remove for you
  void add(Entry *listHead, uint8_t listId, ID selfId, Object *newEntry) {
    const uint8_t oldId = newEntry->freelistId();
    d_assert(oldId != listId);
    d_assert(!newEntry->isLargeAlloc());

    Entry *newEntryFreelist = newEntry->getFreelist();
    if (likely(newEntryFreelist->next().hasValue())) {
      // we will be part of a list every time except the first time add is called after alloc
      newEntryFreelist->remove(listHead);
    }

    newEntry->setFreelistId(listId);

    const ID newEntryId = GetMiniHeapID(newEntry);
    ID lastId = prev();
    Entry *prevList = nullptr;
    if (lastId == list::Head) {
      prevList = this;
    } else {
      Object *last = GetMiniHeap(lastId);
      prevList = last->getFreelist();
    }
    prevList->setNext(newEntryId);
    *newEntry->getFreelist() = ListEntry{lastId, selfId};
    this->setPrev(newEntryId);
  }

  void remove(Entry *listHead) {
    ID prevId = _prev;
    ID nextId = _next;
    // we may have just been created + not added to any freelist yet
    if (!prevId.hasValue() || !nextId.hasValue()) {
      return;
    }
    Entry *prev = nullptr;
    if (prevId == list::Head) {
      prev = listHead;
    } else {
      Object *mh = GetMiniHeap(prevId);
      d_assert(mh != nullptr);
      prev = mh->getFreelist();
    }
    Entry *next = nullptr;
    if (nextId == list::Head) {
      next = listHead;
    } else {
      Object *mh = GetMiniHeap(nextId);
      d_assert(mh != nullptr);
      next = mh->getFreelist();
    }

    prev->setNext(nextId);
    next->setPrev(prevId);
    _prev = MiniHeapID{};
    _next = MiniHeapID{};
  }

private:
  MiniHeapID _prev{};
  MiniHeapID _next{};
};

typedef ListEntry<MiniHeap, MiniHeapID> MiniHeapListEntry;

typedef uint32_t Offset;
typedef uint32_t Length;

struct Span {
  // offset and length are in pages
  explicit Span(Offset _offset, Length _length) : offset(_offset), length(_length) {
  }

  Span(const Span &rhs) : offset(rhs.offset), length(rhs.length) {
  }

  Span &operator=(const Span &rhs) {
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
inline void mwcShuffle(_RandomAccessIterator __first, _RandomAccessIterator __last, _RNG &__rng) {
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
}  // namespace internal
}  // namespace mesh

#endif  // MESH_INTERNAL_H
