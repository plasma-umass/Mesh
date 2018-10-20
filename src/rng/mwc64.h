// -*- C++ -*-

#ifndef MWC64_H_
#define MWC64_H_

#include <stdint.h>

#include "common.h"

class MWC64 {
  uint64_t _x, _c, _t;

  inline void ATTRIBUTE_ALWAYS_INLINE init(uint64_t seed1, uint64_t seed2) {
    _x = seed1;
    _x <<= 32;
    _x += seed2;
    _c = 123456123456123456ULL;
    _index = 2;
  }

  inline uint64_t ATTRIBUTE_ALWAYS_INLINE MWC() {
    _t = (_x << 58) + _c;
    _c = _x >> 6;
    _x += _t;
    _c += (_x < _t);
    return _x;
  }

  int _index;
  uint64_t _value;

public:
  MWC64() {
    auto a = mesh::internal::seed();
    auto b = mesh::internal::seed();
    init(a, b);
  }

  MWC64(uint64_t seed1, uint64_t seed2) {
    init(seed1, seed2);
  }

  inline uint64_t ATTRIBUTE_ALWAYS_INLINE next() {
    if (_index == 2) {
      _value = MWC();
      _index = 0;
    }
    auto v = ((uint64_t *)&_value)[_index];
    _index++;
    return v;
  }
};

static_assert(sizeof(MWC64) == 40, "MWC64 not expected size!");

#endif
