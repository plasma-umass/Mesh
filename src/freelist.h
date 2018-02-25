// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__FREELIST_H
#define MESH__FREELIST_H

#include <iterator>
#include <random>
#include <utility>

#include "rng/mwc.h"

#include "internal.h"

using mesh::debug;

namespace mesh {

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

class Freelist {
private:
  DISALLOW_COPY_AND_ASSIGN(Freelist);

public:
  Freelist() {
  }

  ~Freelist() {
    detach();
  }

  void init(MWC &fastPrng, internal::Bitmap &bitmap) {
    const auto objectCount = _maxCount;
    d_assert_msg(objectCount <= kMaxFreelistLength, "objCount? %zu <= %zu", objectCount, kMaxFreelistLength);
    d_assert(_maxCount == objectCount);
    const size_t listSize = objectCount * sizeof(uint8_t);

    _off = bitmap.inUseCount();

    for (size_t i = 0, off = _off; i < objectCount; i++) {
      // if we were passed in a bitmap and the current object is
      // already allocated, don't add its offset to the freelist
      if (bitmap.isSet(i)) {
        continue;
      } else {
        bitmap.tryToSet(i);
      }

      d_assert(off < objectCount);
      _list[off++] = i;
    }

    if (kEnableShuffleFreelist) {
      mwcShuffle(&_list[0], &_list[objectCount], fastPrng);
      // std::shuffle(&_list[_off], &_list[objectCount], prng);
    }
  }

  void detach() {
    // auto list = _list;
    // _list = nullptr;
    _attachedMiniheap->detach();
    _attachedMiniheap = nullptr;
    _start = 0;
    _end = 0;
    atomic_thread_fence(memory_order_seq_cst);
    // mesh::internal::Heap().free(list);
  }

  inline bool isExhausted() const {
    return _off >= _maxCount;
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  // number of items in the list
  inline size_t length() const {
    return _maxCount - _off;
  }

  // Pushing an element onto the freelist does a round of the
  // Fisher-Yates shuffle.
  inline void push(MWC &prng, size_t freedOff) {
    d_assert(_off > 0);  // we must have at least 1 free space in the list
    _list[--_off] = freedOff;

    if (kEnableShuffleFreelist) {
      size_t swapOff = prng.inRange(_off, maxCount() - 1);

      std::swap(_list[_off], _list[swapOff]);
    }
  }

  inline size_t pop(bool &exhausted) {
    d_assert(_off >= 0 && static_cast<uint16_t>(_off) < _maxCount);
    auto allocOff = _list[_off++];

    exhausted = isExhausted();

    return allocOff;
  }

  inline void free(MWC &prng, void *ptr) {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto off = (ptrval - _start) / _objectSize;

    push(prng, off);
  }

  void freeAllExcept(void *exception) {
    bool exhausted = isExhausted();
    while (!exhausted) {
      auto off = pop(exhausted);
      auto ptr = _attachedMiniheap->ptrForOffset(off);
      if (ptr != exception)
        _attachedMiniheap->free(ptr);
    }
  }

  inline bool isAttached() const {
    return _attachedMiniheap != nullptr;
  }

  inline void attach(MWC &prng, MiniHeap *mh) {
    d_assert(_attachedMiniheap == nullptr);
    _attachedMiniheap = mh;
    _start = mh->getSpanStart();
    _end = _start + mh->spanSize();
    init(prng, mh->writableBitmap());
  }

  inline bool contains(void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return ptrval >= _start && ptrval < _end;
  }

  inline void *malloc(bool &exhausted) {
    const auto off = pop(exhausted);
    // TODO: reduce duplication w/ miniheap
    return reinterpret_cast<void *>(_start + off * _objectSize);
  }

  // FIXME: pull onto freelist?
  inline size_t getSize() {
    return _objectSize;
  }

  inline void setObjectSize(size_t sz) {
    _objectSize = sz;
    _maxCount = max(HL::CPUInfo::PageSize / sz, kMinStringLen);

  }

private:
  size_t _objectSize{0};
  uintptr_t _start{0};
  uintptr_t _end{0};
  MiniHeap *_attachedMiniheap{nullptr};
  uint16_t _maxCount{0};
  uint16_t _off{0};
  uint8_t _list[kMaxFreelistLength];
  uint8_t __padding[24];
};

static_assert(HL::gcd<sizeof(Freelist), CACHELINE_SIZE>::value == CACHELINE_SIZE, "Freelist not multiple of cacheline size!");
}  // namespace mesh

#endif  // MESH__FREELIST_H
