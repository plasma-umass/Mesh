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

template <size_t MaxFreelistLen = sizeof(uint8_t) << 8, typename fl_off_t = uint8_t>
class Freelist {
private:
  DISALLOW_COPY_AND_ASSIGN(Freelist);

public:
  Freelist(size_t objectCount, mt19937_64 &prng, MWC &fastPrng) : _maxCount(objectCount) {
    d_assert(objectCount <= MaxFreelistLen);
    d_assert(_maxCount == objectCount);

    init(prng, fastPrng);
  }

  ~Freelist() {
    detach();
  }

  void init(mt19937_64 &prng, MWC &fastPrng, internal::Bitmap *bitmap = nullptr) {
    const size_t objectCount = maxCount();
    const size_t listSize = objectCount * sizeof(fl_off_t);

    // account for in-use objects if passed a bitmap
    if (bitmap == nullptr)
      _off = 0;
    else
      _off = bitmap->inUseCount();

    _list = reinterpret_cast<fl_off_t *>(mesh::internal::Heap().malloc(listSize));

    for (size_t i = 0, off = _off; i < objectCount; i++) {
      // if we were passed in a bitmap and the current object is
      // already allocated, don't add its offset to the freelist
      if (bitmap != nullptr && bitmap->isSet(i))
        continue;

      d_assert(off < objectCount);
      _list[off++] = i;
    }

    // mwcShuffle(&_list[0], &_list[objectCount], fastPrng);
    std::shuffle(&_list[_off], &_list[objectCount], prng);
  }

  void detach() {
    mesh::internal::Heap().free(_list);
    _list = nullptr;
  }

  inline bool isExhausted() const {
    return _list == nullptr || _off >= _maxCount;
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  // Pushing an element onto the freelist does a round of the
  // Fisher-Yates shuffle.
  inline void push(size_t freedOff, mt19937_64 &prng, MWC &mwc) {
    d_assert(_off > 0);  // we must have at least 1 free space in the list
    _list[--_off] = freedOff;

    size_t swapOff;
    if (mesh::internal::SlowButAccurateRandom) {
      // endpoint is _inclusive_, so we subtract 1 from maxCount since
      // we're dealing with 0-indexed offsets
      std::uniform_int_distribution<size_t> distribution(_off, maxCount() - 1);

      swapOff = distribution(prng);
    } else {
      swapOff = mwc.inRange(_off, maxCount() - 1);
    }

    std::swap(_list[_off], _list[swapOff]);
  }

  inline size_t pop() {
    d_assert(_off >= 0 && static_cast<uint16_t>(_off) < _maxCount);
    auto allocOff = _list[_off++];

    return allocOff;
  }

private:
  fl_off_t *_list{nullptr};
  const uint16_t _maxCount;
  // FIXME: we can use a uint8_t here if we encode "overflowed" as _list == nullptr
  uint16_t _off{0};
};
}  // namespace mesh

#endif  // MESH__FREELIST_H
