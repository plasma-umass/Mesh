// -*- C++ -*-

#ifndef MWC64_H_
#define MWC64_H_

#include <stdint.h>

#include "realrandomvalue.h"

class MWC64 {

  unsigned long long _x, _c, _t;

  void init (unsigned long long seed1, unsigned long long seed2)
  {
    _x = seed1;
    _x <<= 32;
    _x += seed2;
    _c = 123456123456123456ULL;
    _index = 2;
  }

  unsigned long long MWC() {
    _t = (_x << 58) + _c;
    _c = _x >> 6;
    _x += _t;
    _c += (_x < _t);
    return _x;
  }

  int _index;
  unsigned long long _value;

public:
  
  MWC64()
  {
    unsigned int a = RealRandomValue::value();
    unsigned int b = RealRandomValue::value();
    init (a, b);
  }
  
  MWC64 (unsigned long long seed1, unsigned long long seed2)
  {
    init (seed1, seed2);
  }
  
  inline unsigned long next()
  {
    if (_index == 2) {
      _value = MWC();
      _index = 0;
    }
    unsigned long v = ((unsigned long *) &_value)[_index];
    _index++;
    return v;
  }

};

#endif
