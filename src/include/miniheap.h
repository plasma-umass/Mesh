// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_MINIHEAP_H
#define MESH_MINIHEAP_H

#include <mutex>
#include <random>

#include "heaplayers.h"
#include "internal.h"

using std::lock_guard;
using std::mutex;
using std::uniform_int_distribution;

namespace mesh {

uint64_t seed() {
  static char muBuf[sizeof(std::mutex)];
  static std::mutex *mu = new (muBuf) std::mutex();

  static char mtBuf[sizeof(std::mt19937_64)];
  static std::mt19937_64 *mt = NULL;

  lock_guard<mutex> lock(*mu);

  if (likely(mt != nullptr))
    return (*mt)();

  // first time requesting a seed
  std::random_device rd;
  mt = new (mtBuf) std::mt19937_64(rd());

  static_assert(sizeof(std::mt19937_64::result_type) == sizeof(uint64_t), "expected 64-bit result_type for PRNG");

  return (*mt)();
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
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);

public:
  enum { Alignment = (int)MinObjectSize };
  static const size_t span_size = SpanSize;

  MiniHeapBase(size_t objectSize) : _objectSize(objectSize), _prng(seed()) {
    _span = _super.malloc(SpanSize);
    if (!_span)
      abort();

    _maxCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _maxCount / FullDenominator;

    _bitmap.reserve(_maxCount);

    constexpr auto heapPages = SpanSize / PageSize;
    debug("MiniHeap(%zu): reserving %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p-%p\n", objectSize,
          _maxCount, heapPages, FullNumerator, FullDenominator, _fullCount, this->isFull(), _inUseCount, _span,
          reinterpret_cast<uintptr_t>(_span) + SpanSize);
  }

  inline void *malloc(size_t sz) {
    d_assert(!_done && !isFull());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    // endpoint is _inclusive_, so we subtract 1 from maxCount since
    // we're dealing with 0-indexed offsets
    uniform_int_distribution<size_t> distribution(0, _maxCount - 1);

    // because we are not full, this is guaranteed to terminate
    while (true) {
      size_t off = distribution(_prng);

      if (!_bitmap.tryToSet(off))
        continue;

      _inUseCount++;

      // we JUST set the bitmap
      auto ptr = reinterpret_cast<void *>((uintptr_t)_span + off * _objectSize);

      auto ptrval = reinterpret_cast<uintptr_t>(ptr);
      auto spanStart = reinterpret_cast<uintptr_t>(_span);
      d_assert_msg(ptrval + sz <= spanStart + SpanSize,
                   "OOB alloc? sz:%zu (%p-%p) ptr:%p rand:%zu count:%zu osize:%zu\n", sz, _span, spanStart + SpanSize,
                   ptrval, random, _maxCount, _objectSize);

      return ptr;
    }
  }

  inline void free(void *ptr) {
    d_assert(getSize(ptr) == _objectSize);

    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    auto off = (ptrval - span) / _objectSize;
    if (span > ptrval || off >= _maxCount) {
      debug("MiniHeap(%p): invalid free of %p", this, ptr);
      return;
    }

    if (unlikely(!_bitmap.isSet(off))) {
      debug("MiniHeap(%p): double free of %p", this, ptr);
      return;
    }

    _bitmap.reset(off);
    _inUseCount--;

    if (_inUseCount == 0 && _done) {
      debug("MiniHeap(%p): FREE %4d/%4d (%f) -- size %zu", this, _inUseCount, _maxCount,
            (double)_inUseCount / _maxCount, _objectSize);
      _super.free(_span);
      _span = reinterpret_cast<void *>(0xdeadbeef);
    }
  }

  inline size_t getSize(void *ptr) {
    // auto span = reinterpret_cast<uintptr_t>(_span);
    // auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    // d_assert_msg(span <= ptrval && ptrval < span + SpanSize, "span(%p) <= %p < %p", span, ptrval, span + SpanSize);

    return _objectSize;
  }

  inline bool isFull() {
    return _inUseCount >= _fullCount;
  }

  inline uintptr_t getSpanStart() {
    return reinterpret_cast<uintptr_t>(_span);
  }

  inline void setDone() {
    _done = true;
  }

  inline bool isDone() {
    return _done;
  }

  inline bool isEmpty() {
    return _inUseCount == 0;
  }

  void *_span;
  size_t _objectSize;
  size_t _maxCount{0};
  size_t _fullCount{0};
  size_t _inUseCount{0};
  mt19937_64 _prng;
  BitMap<InternalAlloc> _bitmap{};
  bool _done{false};

  SuperHeap _super{};
};
}

#endif  // MESH_MINIHEAP_H
