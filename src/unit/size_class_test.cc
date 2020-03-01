// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdalign.h>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

#include "common.h"
#include "internal.h"

using namespace mesh;

#define roundtrip(n) ASSERT_TRUE(n == SizeMap::ByteSizeForClass(SizeMap::SizeClass(n)))
#define rt_debug(n) \
  debug("%d c2s: %zu s2c: %d", n, SizeMap::ByteSizeForClass(SizeMap::SizeClass(n)), SizeMap::SizeClass(n))

#define pow2Roundtrip(n) ASSERT_TRUE(n == powerOfTwo::ByteSizeForClass(powerOfTwo::ClassForByteSize(n)))

TEST(SizeClass, MinObjectSize) {
  ASSERT_EQ(alignof(max_align_t), kMinObjectSize);

  ASSERT_EQ(kMinObjectSize, 16UL);

  ASSERT_EQ(staticlog(kMinObjectSize), 4);
}

TEST(SizeClass, SmallClasses) {
  roundtrip(16);
  // rt_debug(16);

  // ASSERT_TRUE(size2Class(8) >= 0);
  // roundtrip(8);
  // rt_debug(8);

  roundtrip(32);
}

TEST(SizeClass, PowerOfTwo) {
  ASSERT_TRUE(powerOfTwo::kMinObjectSize == 8);
  ASSERT_TRUE(powerOfTwo::ClassForByteSize(8) >= 0);

  pow2Roundtrip(8);
  pow2Roundtrip(16);
  pow2Roundtrip(32);
}

TEST(SizeClass, Reciprocal) {
  for (size_t i = 0; i < kClassSizesMax; i++) {
    volatile const size_t objectSize = SizeMap::class_to_size(i);
    // volatile to avoid the compiler compiling it away
    volatile const float recip = 1.0 / (float)objectSize;

    for (size_t j = 0; j <= kPageSize; j += 8) {
      // we depend on this floating point calcuation always being
      // equivalent to the integer division operation
      volatile const size_t off = j * recip;
      volatile const size_t off2 = j / objectSize;
      ASSERT_TRUE(off == off2);
    }

    const size_t newObjectSize = __builtin_roundf(1 / recip);
    ASSERT_TRUE(newObjectSize == objectSize);
  }
}
