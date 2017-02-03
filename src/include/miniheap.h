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
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);

public:
  enum { Alignment = (int)MinObjectSize };
  static const size_t span_size = SpanSize;

  MiniHeapBase(size_t objectSize)
    : _objectSize(objectSize), _maxCount(), _fullCount(), _inUseCount(),
      _rng(seed(), seed()), _bitmap(), _done(false), _super() {

    d_assert(_inUseCount == 0);

    _span = _super.malloc(SpanSize);
    if (!_span)
      abort();

    d_assert(_inUseCount == 0);

    constexpr auto heapPages = SpanSize / PageSize;
    _maxCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _maxCount / FullDenominator;

    _bitmap.reserve(_maxCount);

    // for error detection
    memset(_span, 'S', SpanSize);

    debug("MiniHeap(%zu): reserving %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p-%p\n",
          objectSize, _maxCount, heapPages, FullNumerator, FullDenominator, _fullCount,
          this->isFull(), _inUseCount, _span, reinterpret_cast<uintptr_t>(_span)+SpanSize);
  }

  inline void *malloc(size_t sz) {
    d_assert(!_done && !isFull());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    // because we are not full, this is guaranteed to terminate
    while (true) {
      size_t off = _rng.next() % _maxCount;

      if (!_bitmap.tryToSet(off))
        continue;

      _inUseCount++;

      // we JUST set the bitmap
      auto ptr = reinterpret_cast<void *>((uintptr_t)_span + off * _objectSize);

      auto ptrval = reinterpret_cast<uintptr_t>(ptr);
      auto spanStart = reinterpret_cast<uintptr_t>(_span);
      d_assert_msg(ptrval+sz <= spanStart+SpanSize,
                   "OOB alloc? sz:%zu (%p-%p) ptr:%p rand:%zu count:%zu osize:%zu\n", sz, _span, spanStart+SpanSize, ptrval, random, _maxCount, _objectSize);

      char *charPtr = reinterpret_cast<char *>(ptr);
      for (size_t i = 0; i < _objectSize; i++) {
        d_assert_msg(charPtr[i] == 'S' || charPtr[i] == 'F',
                     "MiniHeap(%p): on span %p (sz:%zu) %zu not 'F' or 'S': %s", this, _span, _objectSize, i, charPtr[i]);
      }
      memset(ptr, 'A', sz);

      {
        d_assert(getSize(ptr) == _objectSize);

        auto span = reinterpret_cast<uintptr_t>(_span);
        auto ptrval = reinterpret_cast<uintptr_t>(ptr);

        d_assert(span <= ptrval);
        auto new_off = (ptrval - span)/_objectSize;
        d_assert_msg(new_off == off, "off calc fucked up %zu %zu", off, new_off);
        d_assert(new_off < _maxCount);
      }

      d_assert(_bitmap.isSet(off));
      return ptr;
    }
  }

  inline void free(void *ptr) {
    d_assert(_inUseCount >= 0);
    d_assert(getSize(ptr) == _objectSize);

    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    d_assert(span <= ptrval);
    auto off = (ptrval - span)/_objectSize;

    d_assert(off < _maxCount);
    d_assert_msg(_bitmap.isSet(off), "MiniHeap(%p): caught double free of %p?", this, ptr);

    // scribble to try to catch problems faster
    memset(ptr, 'F', _objectSize);

    _bitmap.reset(off);
    d_assert(!_bitmap.isSet(off));
    
    _inUseCount--;

    // if (_done) {
    //   debug("MiniHeap(%p): FREE %4d/%4d (%f) -- size %zu", this, _inUseCount, _maxCount, (double)_inUseCount/_maxCount, _objectSize);
    // }
    if (_inUseCount == 0 && _done) {
      debug("MiniHeap(%p): FREE %4d/%4d (%f) -- size %zu", this, _inUseCount, _maxCount, (double)_inUseCount/_maxCount, _objectSize);
      _super.free(_span);
      //_span = (void *)0xdeadbeef;
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
  size_t _maxCount;
  size_t _fullCount;
  size_t _inUseCount;
  MWC _rng;
  BitMap<InternalAlloc> _bitmap;
  bool _done;

  SuperHeap _super;
};

#endif  // MESH_MINIHEAP_H
