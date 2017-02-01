// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_MINIHEAP_H
#define MESH_MINIHEAP_H

#include <mutex>
#include <random>

#include "heaplayers.h"
#include "internal.h"
#include "rng/mwc.h"

using namespace HL;

uint32_t seed() {
  // single random_device to seed the random number generators in MiniHeaps
  static std::random_device rd;
  static std::mutex rdLock;

  std::lock_guard<std::mutex> lock(rdLock);

  uint32_t seed = 0;
  while (seed == 0) {
    seed = rd();
  }
  return seed;
}

template <typename SuperHeap,
          typename InternalAlloc,
          size_t PageSize = 4096,
          size_t MinObjectSize = 16,
          size_t MaxObjectSize = 2048,
          size_t MinAvailablePages = 4,
          size_t SpanSize = 128UL * 1024UL, // 128Kb spans
          unsigned int FullNumerator = 3,
          unsigned int FullDenominator = 4>
class MiniHeapBase : public SuperHeap {
public:
  enum { Alignment = (int)MinObjectSize };
  static const size_t span_size = SpanSize;

  MiniHeapBase(size_t objectSize)
      : SuperHeap(), _objectSize(objectSize), _objectCount(), _inUseCount(), _fullCount(),
        _rng(seed(), seed()), _bitmap() {

    d_assert(_inUseCount == 0);
    _span = SuperHeap::malloc(SpanSize);
    if (!_span)
      abort();

    d_assert(_inUseCount == 0);

    constexpr auto heapPages = SpanSize / PageSize;
    _objectCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _objectCount / FullDenominator;

    _bitmap.reserve(_objectCount);

    debug("MiniHeap(%zu): reserving %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p-%p\n",
          objectSize, _objectCount, heapPages, FullNumerator, FullDenominator, _fullCount,
          this->isFull(), _inUseCount, _span, reinterpret_cast<uintptr_t>(_span)+SpanSize);
  }

  inline void *malloc(size_t sz) {
    d_assert_msg(sz <= _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    // should never have been called
    if (isFull())
      abort();

    while (true) {
      //auto random = _rng.next() % _objectCount;
      size_t random = seed() % _objectCount;

      if (_bitmap.tryToSet(random)) {
        auto ptr = reinterpret_cast<void *>((uintptr_t)_span + random * _objectSize);
        _inUseCount++;
        auto ptrval = reinterpret_cast<uintptr_t>(ptr);
        auto spanStart = reinterpret_cast<uintptr_t>(_span);
        d_assert_msg(ptrval+sz <= spanStart+SpanSize,
                     "OOB alloc? sz:%zu (%p-%p) ptr:%p rand:%zu count:%zu osize:%zu\n", sz, _span, spanStart+SpanSize, ptrval, random, _objectCount, _objectSize);
        return ptr;
      }
    }
  }

  inline void free(void *ptr) {
    d_assert(getSize(ptr) == _objectSize);

    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    auto off = (ptrval - span)/_objectSize;
    d_assert(0 <= off && off < _objectCount);

    if (_bitmap.isSet(off)) {
      _bitmap.reset(off);
      _inUseCount--;
      if (_inUseCount == 0) {
        debug("FIXME: free span");
      }
    } else {
      debug("MiniHeap(%p): caught double free of %p?", this, ptrval);
    }
  }

  inline size_t getSize(void *ptr) {
    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    d_assert_msg(span <= ptrval && ptrval < span + SpanSize, "span(%p) <= %p < %p", span, ptrval, span + SpanSize);

    return _objectSize;
  }

  inline bool isFull() {
    return _inUseCount >= _fullCount;
  }

  inline uintptr_t getSpanStart() {
    return reinterpret_cast<uintptr_t>(_span);
  }

  void *_span;
  size_t _objectSize;
  size_t _objectCount;
  size_t _inUseCount;
  size_t _fullCount;
  MWC _rng;
  BitMap<InternalAlloc> _bitmap;
};

#endif  // MESH_MINIHEAP_H
