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
  Freelist(size_t objectCount) : _maxCount(objectCount) {
    d_assert_msg(objectCount <= kMaxFreelistLength, "objCount? %zu <= %zu", objectCount, kMaxFreelistLength);
    d_assert(_maxCount == objectCount);
  }

  ~Freelist() {
    detach();
  }

  void init(mt19937_64 &prng, MWC &fastPrng, internal::Bitmap &bitmap) {
    const size_t objectCount = maxCount();
    const size_t listSize = objectCount * sizeof(uint8_t);

    _off = bitmap.inUseCount();

    _list = reinterpret_cast<uint8_t *>(mesh::internal::Heap().malloc(listSize));

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
      // mwcShuffle(&_list[0], &_list[objectCount], fastPrng);
      std::shuffle(&_list[_off], &_list[objectCount], prng);
    }
  }

  void detach() {
    auto list = _list;
    _list = nullptr;
    atomic_thread_fence(memory_order_seq_cst);
    mesh::internal::Heap().free(list);
  }

  inline bool isNonNullExhausted() const {
    return _off >= _maxCount;
  }
  inline bool isExhausted() const {
    return _list == nullptr || isNonNullExhausted();
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
  inline void push(size_t freedOff, mt19937_64 &prng, MWC &mwc) {
    d_assert(_off > 0);  // we must have at least 1 free space in the list
    _list[--_off] = freedOff;

    if (kEnableShuffleFreelist) {
      size_t swapOff;
      if (kSlowButAccurateRandom) {
        // endpoint is _inclusive_, so we subtract 1 from maxCount since
        // we're dealing with 0-indexed offsets
        std::uniform_int_distribution<size_t> distribution(_off, maxCount() - 1);

        swapOff = distribution(prng);
      } else {
        swapOff = mwc.inRange(_off, maxCount() - 1);
      }

      std::swap(_list[_off], _list[swapOff]);
    }
  }

  inline size_t pop(bool &isExhausted) {
    d_assert(_off >= 0 && static_cast<uint16_t>(_off) < _maxCount);
    auto allocOff = _list[_off++];

    isExhausted = isNonNullExhausted();

    return allocOff;
  }

private:
  uint8_t *_list{nullptr};
  const uint16_t _maxCount;
  // FIXME: we can use a uint8_t here if we encode "overflowed" as _list == nullptr
  uint16_t _off{0};
};
}  // namespace mesh

#endif  // MESH__FREELIST_H
