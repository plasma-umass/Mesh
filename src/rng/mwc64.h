// -*- C++ -*-

#ifndef MESH_MWC64_H
#define MESH_MWC64_H

#include <stdint.h>

#include "../common.h"

class MWC64 {
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

  uint64_t _x;
  uint64_t _c;
  uint64_t _t;
  uint64_t _value;
  int _index;

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
    // grab either the top or bottom 32-bits of the 64-bit _value
    uint32_t v = ((uint32_t *)&_value)[_index];
    _index++;
    return v;
  }
};

static_assert(sizeof(MWC64) == 40, "MWC64 not expected size!");

#endif
