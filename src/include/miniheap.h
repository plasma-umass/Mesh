// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_MINIHEAP_H
#define MESH_MINIHEAP_H

#include <random>

#include "bitmap.h"
#include "internal.h"

#include "heaplayers.h"

using std::uniform_int_distribution;

namespace mesh {

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

  MiniHeapBase(size_t objectSize) : _objectSize(objectSize), _prng(internal::seed()) {
    _span = _super.malloc(SpanSize);
    if (!_span)
      abort();

    _maxCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _maxCount / FullDenominator;

    _bitmap.reserve(_maxCount);
  }

  void dumpDebug() const {
    constexpr auto heapPages = SpanSize / PageSize;
    debug("MiniHeap(%p:%5zu): %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p-%p\n",
          this, _objectSize, _maxCount, heapPages, FullNumerator, FullDenominator, _fullCount, this->isFull(),
          _inUseCount, _span, reinterpret_cast<uintptr_t>(_span) + SpanSize);
  }

  inline void *malloc(size_t sz) {
    d_assert(!_done && !isFull());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    // endpoint is _inclusive_, so we subtract 1 from maxCount since
    // we're dealing with 0-indexed offsets
    uniform_int_distribution<size_t> distribution(0, _maxCount - 1);

    // because our span is not full and no other malloc is running
    // concurrently on this span, this is guaranteed to terminate
    while (true) {
      size_t off = distribution(_prng);

      if (!_bitmap.tryToSet(off))
        continue;

      _inUseCount++;

      return reinterpret_cast<void *>(getSpanStart() + off * _objectSize);
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
      _super.free(_span);
    }
  }

  inline bool contains(void *ptr) const {
    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return span <= ptrval && ptrval < span + SpanSize;
  }

  inline size_t getSize(void *ptr) const {
    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span, ptr, reinterpret_cast<uintptr_t>(_span) + SpanSize);

    return _objectSize;
  }

  inline bool isFull() const {
    return _inUseCount >= _fullCount;
  }

  inline uintptr_t getSpanStart() const {
    return reinterpret_cast<uintptr_t>(_span);
  }

  inline void setDone() {
    _done = true;
  }

  inline bool isDone() const {
    return _done;
  }

  inline bool isEmpty() const {
    return _inUseCount == 0;
  }

  const Bitmap<InternalAlloc> &bitmap() const {
    return _bitmap;
  }

  void *_span;
  size_t _objectSize;
  size_t _maxCount{0};
  size_t _fullCount{0};
  size_t _inUseCount{0};
  mt19937_64 _prng;
  Bitmap<InternalAlloc> _bitmap{};
  bool _done{false};

  SuperHeap _super{};
};
}

#endif  // MESH_MINIHEAP_H
