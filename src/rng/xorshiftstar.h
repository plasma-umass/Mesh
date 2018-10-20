#include <stdint.h>

#include "common.h"

class XorshiftStar {
private:
  DISALLOW_COPY_AND_ASSIGN(XorshiftStar);

public:
  explicit XorshiftStar(uint64_t seed1, uint64_t seed2) {
    _s[0] = seed1;
    _s[1] = seed2;
  }

  inline uint64_t ATTRIBUTE_ALWAYS_INLINE next() {
    uint64_t x = _s[0];
    uint64_t const y = _s[1];
    _s[0] = y;
    x ^= x << 23;                           // a
    _s[1] = x ^ y ^ (x >> 17) ^ (y >> 26);  // b, c
    return _s[1] + y;
  }

  inline uint64_t ATTRIBUTE_ALWAYS_INLINE inRange(size_t min, size_t max) {
    const auto result = min + next() % (1 + max - min);

    return result;
  }

private:
  uint64_t _s[2];
};
