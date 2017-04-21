// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__FREELIST_H
#define MESH__FREELIST_H

#include <atomic>
#include <random>

#include "bitmap.h"
#include "internal.h"

#include "rng/mwc.h"

using mesh::debug;

// namespace mesh {

template <size_t MaxFreelistLen = sizeof(uint8_t) << 8>
class Freelist {
private:
  DISALLOW_COPY_AND_ASSIGN(Freelist);

public:
  Freelist(size_t objectCount, mt19937_64 &prng) : _list(), _maxCount(objectCount) {
    d_assert(objectCount <= MaxFreelistLen);
    d_assert(_maxCount == objectCount);

    _list = reinterpret_cast<uint8_t *>(mesh::internal::Heap().malloc(objectCount));
    for (size_t i = 0; i < objectCount; i++) {
      _list[i] = i;
    }

    std::shuffle(&_list[0], &_list[objectCount], prng);
  }

  void detach() {
    mesh::internal::Heap().free(_list);
    _list = nullptr;
  }

  ~Freelist() {
    detach();
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

    const uint8_t a = _list[_off];
    const uint8_t b = _list[swapOff];

    _list[_off] = b;
    _list[swapOff] = a;
  }

  inline size_t pop() {
    auto allocOff = _list[_off];
    _off += 1;

    return allocOff;
  }

private:
  uint8_t *_list;
  const uint16_t _maxCount;
  uint8_t _off{0};
};
//}  // namespace mesh

#endif  // MESH__FREELIST_H
