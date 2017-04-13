// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINIHEAP_H
#define MESH__MINIHEAP_H

#include <atomic>
#include <random>

#include "bitmap.h"
#include "internal.h"

#include "rng/mwc.h"

#include "heaplayers.h"

// namespace mesh {

using std::atomic_size_t;

// class Freelist {
// private:
//   DISALLOW_COPY_AND_ASSIGN(Freelist);
//
// public:
//   Freelist(maxCount) : _list() {
//     _list = reinterpret_cast<uint8_t *>(mesh::internal::Heap().malloc(maxCount));
//   }
//
// private:
//   uint8_t *_list;
// }

template <size_t MaxFreelistLen = sizeof(uint8_t) << 8,  // AKA max # of objects per miniheap
          size_t MaxMeshes = 4>                          // maximum number of VM spans we can track
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);

public:
  MiniHeapBase(void *span, size_t objectCount, size_t objectSize, mt19937_64 &prng, size_t expectedSpanSize)
      : _span{reinterpret_cast<char *>(span)},
        _maxCount(objectCount),
        _objectSize(objectSize),
        _meshCount(1),
        _done(false),
        _bitmap(maxCount()) {
    if (!_span[0])
      abort();

    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 _maxCount, _objectSize);

    d_assert(_maxCount == objectCount);
    // d_assert(_objectSize == objectSize);

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    d_assert_msg(objectSize == static_cast<size_t>(_objectSize), "%zu != %hu", objectSize, _objectSize);

    d_assert(maxCount() <= MaxFreelistLen);
    d_assert(_span[1] == nullptr);

    _freeList = reinterpret_cast<uint8_t *>(mesh::internal::Heap().malloc(maxCount()));

    const auto objCount = maxCount();
    for (size_t i = 0; i < objCount; i++) {
      _freeList[i] = i;
    }

    std::shuffle(&_freeList[0], &_freeList[maxCount()], prng);

    // dumpDebug();
  }

  ~MiniHeapBase() {
    mesh::internal::Heap().free(_freeList);
    _freeList = nullptr;
  }

  void dumpDebug() const {
    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = _inUseCount;
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (full: %d, inUse: %zu)\t%p-%p\n", this, _objectSize,
                maxCount(), heapPages, this->isFull(), inUseCount, _span[0],
                reinterpret_cast<uintptr_t>(_span) + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string().c_str());
  }

  inline void *mallocAt(size_t off) {
    if (!_bitmap.tryToSet(off)) {
      mesh::debug("%p: MA %u", this, off);
      dumpDebug();
      return nullptr;
    }

    _inUseCount++;

    return reinterpret_cast<void *>(getSpanStart() + off * _objectSize);
  }

  inline void *malloc(size_t sz) {
    d_assert_msg(!_done && !isFull(), "done: %d, full: %d", _done, isFull());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    auto off = _freeList[_freeListOff++];
    // mesh::debug("%p: ma %u", this, off);

    auto ptr = mallocAt(off);
    d_assert(ptr != nullptr);

    return ptr;
  }

  inline ssize_t getOff(void *ptr) const {
    d_assert(getSize(ptr) == _objectSize);

    const auto span = spanStart(ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    const auto off = (ptrval - span) / _objectSize;
    if (span > ptrval || off >= maxCount()) {
      mesh::debug("MiniHeap(%p): invalid free of %p", this, ptr);
      return -1;
    }

    if (unlikely(!_bitmap.isSet(off))) {
      mesh::debug("MiniHeap(%p): double free of %p", this, ptr);
      return -1;
    }

    return off;
  }

  inline void localFree(void *ptr, mt19937_64 &prng, MWC &mwc) {
    const ssize_t freedOff = getOff(ptr);
    if (freedOff < 0)
      return;

    d_assert(_freeListOff > 0);
    _freeListOff--;
    _freeList[_freeListOff] = freedOff;

    _bitmap.unset(freedOff);
    _inUseCount--;

    size_t swapOff;

    if (mesh::internal::SlowButAccurateRandom) {
      // endpoint is _inclusive_, so we subtract 1 from maxCount since
      // we're dealing with 0-indexed offsets
      std::uniform_int_distribution<size_t> distribution(_freeListOff, maxCount() - 1);

      swapOff = distribution(prng);
    } else {
      swapOff = mwc.inRange(_freeListOff, maxCount() - 1);
    }

    const uint8_t swapped = _freeList[swapOff];
    _freeList[swapOff] = _freeList[_freeListOff];
    _freeList[_freeListOff] = swapped;
  }

  inline void free(void *ptr) {
    const size_t off = getOff(ptr);
    if (off < 0)
      return;

    _bitmap.unset(off);
    _inUseCount--;
  }

  inline bool shouldReclaim() const {
    return _inUseCount == 0 && _done;
  }

  inline uintptr_t spanStart(void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    for (size_t i = 0; i < _meshCount; ++i) {
      d_assert(_span[i] != nullptr);
      const auto span = reinterpret_cast<uintptr_t>(_span[i]);
      if (span <= ptrval && ptrval < span + spanSize())
        return span;
    }

    return 0;
  }

  inline bool contains(void *ptr) const {
    return spanStart(ptr) != 0;
  }

  inline size_t spanSize() const {
    size_t bytesNeeded = static_cast<size_t>(_objectSize) * static_cast<size_t>(_maxCount);
    return mesh::RoundUpToPage(bytesNeeded);
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  inline size_t objectSize() const {
    return _objectSize;
  }

  inline size_t getSize(void *ptr) const {
    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span[0], ptr,
                 reinterpret_cast<uintptr_t>(_span[0]) + spanSize());

    return objectSize();
  }

  inline bool isFull() const {
    return _inUseCount >= maxCount();
  }

  inline uintptr_t getSpanStart() const {
    return reinterpret_cast<uintptr_t>(_span[0]);
  }

  inline void setDone() {
    mesh::internal::Heap().free(_freeList);
    _freeList = nullptr;
    _done = true;
  }

  inline bool isDone() const {
    return _done;
  }

  inline bool isEmpty() const {
    return _inUseCount == 0;
  }

  inline size_t inUseCount() const {
    return _inUseCount;
  }

  const mesh::internal::Bitmap &bitmap() const {
    return _bitmap;
  }

  void meshedSpan(uintptr_t spanStart) {
    if (_meshCount >= MaxMeshes) {
      mesh::debug("fatal: too many meshes for one miniheap");
      dumpDebug();
      abort();
    }

    _span[_meshCount] = reinterpret_cast<char *>(spanStart);
    _meshCount++;
  }

  size_t meshCount() const {
    return _meshCount;
  }

  char *const *spans() const {
    return _span;
  }

  void insertPrev(MiniHeapBase<MaxFreelistLen, MaxMeshes> *mh) {
  }

  void insertNext(MiniHeapBase<MaxFreelistLen, MaxMeshes> *mh) {
  }

  MiniHeapBase<MaxFreelistLen, MaxMeshes> *next() {
    return _next;
  }

  MiniHeapBase<MaxFreelistLen, MaxMeshes> *remove() {
    if (_prev != nullptr)
      _prev->_next = _next;

    if (_next != nullptr)
      _next->_prev = _prev;

    return _next;
  }

private:
  char *_span[MaxMeshes];
  uint8_t *_freeList;
  const uint16_t _maxCount;
  const uint16_t _objectSize;
  atomic_uint16_t _inUseCount{0};
  uint8_t _freeListOff{0};
  uint8_t _meshCount : 7;
  uint8_t _done : 1;
  mesh::internal::Bitmap _bitmap;
  MiniHeapBase<MaxFreelistLen, MaxMeshes> *_prev{nullptr};
  MiniHeapBase<MaxFreelistLen, MaxMeshes> *_next{nullptr};
};

typedef MiniHeapBase<> MiniHeap;

static_assert(sizeof(mesh::internal::Bitmap) == 16, "Bitmap too big!");
// static_assert(sizeof(MiniHeap) <= 64, "MiniHeap too big!");

//}  // namespace mesh

#endif  // MESH__MINIHEAP_H
