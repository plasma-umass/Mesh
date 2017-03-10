// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINIHEAP_H
#define MESH__MINIHEAP_H

#include <random>

#include "bitmap.h"
#include "internal.h"

#include "heaplayers.h"

namespace mesh {

template <unsigned int FullNumerator = 3, unsigned int FullDenominator = 4>
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);

public:
  MiniHeapBase(void *span, size_t spanSize, size_t objectSize)
      : _span(span), _spanSize(spanSize), _objectSize(objectSize), _bitmap(maxCount()) {
    if (!_span)
      abort();

    dumpDebug();
  }

  void dumpDebug() const {
    const auto heapPages = _spanSize / HL::CPUInfo::PageSize;
    debug("MiniHeap(%p:%5zu): %zu objects on %zu pages (%u/%u full: %zu/%d inUse: %zu)\t%p-%p\n", this, _objectSize,
          maxCount(), heapPages, FullNumerator, FullDenominator, fullCount(), this->isFull(), _inUseCount, _span,
          reinterpret_cast<uintptr_t>(_span) + _spanSize);
  }

  static void mesh(MiniHeapBase *dst, MiniHeapBase *src) {
    uintptr_t srcSpan = src->getSpanStart();
    uintptr_t dstSpan = dst->getSpanStart();
    auto objectSize = dst->_objectSize;

    // for each object in src, copy it to dst + update dst's bitmap
    // and in-use count
    for (auto const &off : src->bitmap()) {
      debug("mesh offset: %zu", off);
      d_assert(!dst->_bitmap.isSet(off));
      void *dstObject = reinterpret_cast<void *>(dstSpan + off * objectSize);
      void *srcObject = reinterpret_cast<void *>(srcSpan + off * objectSize);
      memcpy(dstObject, srcObject, objectSize);
      dst->_inUseCount++;
      bool ok = dst->_bitmap.tryToSet(off);
      d_assert(ok && dst->_bitmap.isSet(off));
    }

    debug("TODO: MiniHeap::mesh");
    // dst->_super.mesh(dst->_span, src->_span);
  }

  inline void *malloc(mt19937_64 &prng, size_t sz) {
    d_assert(!_done && !isFull());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    // endpoint is _inclusive_, so we subtract 1 from maxCount since
    // we're dealing with 0-indexed offsets
    std::uniform_int_distribution<size_t> distribution(0, maxCount() - 1);

    // because our span is not full and no other malloc is running
    // concurrently on this span, this is guaranteed to terminate
    while (true) {
      size_t off = distribution(prng);

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
    if (span > ptrval || off >= maxCount()) {
      debug("MiniHeap(%p): invalid free of %p", this, ptr);
      return;
    }

    if (unlikely(!_bitmap.isSet(off))) {
      debug("MiniHeap(%p): double free of %p", this, ptr);
      return;
    }

    _bitmap.unset(off);
    _inUseCount--;
  }

  inline bool shouldReclaim() const {
    return _inUseCount == 0 && _done;
  }

  inline bool contains(void *ptr) const {
    auto span = reinterpret_cast<uintptr_t>(_span);
    auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return span <= ptrval && ptrval < span + _spanSize;
  }

  inline size_t maxCount() const {
    return _spanSize / _objectSize;
  }

  inline size_t fullCount() const {
    return FullNumerator * maxCount() / FullDenominator;
  }

  inline size_t getSize(void *ptr) const {
    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span, ptr, reinterpret_cast<uintptr_t>(_span) + _spanSize);

    return _objectSize;
  }

  inline bool isFull() const {
    return _inUseCount >= fullCount();
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

  const internal::Bitmap &bitmap() const {
    return _bitmap;
  }

  void *_span;
  const size_t _spanSize;
  const size_t _objectSize;
  size_t _inUseCount{0};
  internal::Bitmap _bitmap;
  bool _done{false};
};

typedef MiniHeapBase<> MiniHeap;
}

#endif  // MESH__MINIHEAP_H
