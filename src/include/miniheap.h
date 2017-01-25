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
class MiniHeap : public SuperHeap {
public:
  enum { Alignment = (int)MinObjectSize };

  MiniHeap(size_t objectSize)
      : SuperHeap(), _objectSize(objectSize), _objectCount(), _inUseCount(), _fullCount(),
        _rng(seed(), seed()), _bitmap() {

    assert(_inUseCount == 0);
    _span = SuperHeap::malloc(SpanSize);
    if (!_span)
      abort();

    assert(_inUseCount == 0);

    constexpr auto heapPages = SpanSize / PageSize;
    _objectCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _objectCount / FullDenominator;

    _bitmap.reserve(_objectCount);

    debug("MiniHeap(%zu): reserving %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p\n",
          objectSize, _objectCount, heapPages, FullNumerator, FullDenominator, _fullCount,
          this->isFull(), _inUseCount, this);
  }

  inline void *malloc(size_t sz) {
    assert(sz <= _objectSize);

    // should never have been called
    if (isFull())
      abort();

    while (true) {
      //auto random = _rng.next() % _objectCount;
      auto random = seed() % _objectCount;

      if (_bitmap.tryToSet(random)) {
        auto ptr = reinterpret_cast<void *>((uintptr_t)_span + random * _objectSize);
        _inUseCount++;
        return ptr;
      }
    }
  }

  inline void free(void *ptr) {
  }

  inline size_t getSize(void *ptr) {
    auto ptrval = (uintptr_t)ptr;
    if (ptrval < (uintptr_t)_span || ptrval >= (uintptr_t)_span + SpanSize)
      return 0;

    return _objectSize;
  }

  inline bool isFull() {
    return _inUseCount >= _fullCount;
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
