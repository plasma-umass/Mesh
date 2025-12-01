// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdalign.h>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

#include "common.h"
#include "internal.h"
#include "size_class_reciprocals.h"

using namespace mesh;

#define roundtrip(n) ASSERT_TRUE(n == SizeMap::ByteSizeForClass(SizeMap::SizeClass(n)))
#define rt_debug(n) \
  debug("%d c2s: %zu s2c: %d", n, SizeMap::ByteSizeForClass(SizeMap::SizeClass(n)), SizeMap::SizeClass(n))

#define pow2Roundtrip(n) ASSERT_TRUE(n == powerOfTwo::ByteSizeForClass(powerOfTwo::ClassForByteSize(n)))

TEST(SizeClass, MinObjectSize) {
  // kMinObjectSize must be at least as large as the platform's maximum alignment
  // to ensure all allocations are properly aligned for all C/C++ types.
  // It can be larger (e.g., 16 on ARM64 where max_align_t is 8) for consistency
  // across platforms and to simplify size class logic.
  ASSERT_GE(kMinObjectSize, alignof(max_align_t));

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

    for (size_t j = 0; j <= getPageSize(); j += 8) {
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

TEST(SizeClass, ReciprocalTable) {
  // Verify that the shared reciprocal table gives correct results
  // for all size classes and all valid byte offsets
  for (size_t i = 0; i < kClassSizesMax; i++) {
    const size_t objectSize = SizeMap::class_to_size(i);

    // Table reciprocal should match computed reciprocal
    const float tableRecip = float_recip::getReciprocal(i);
    const float expectedRecip = 1.0f / static_cast<float>(objectSize);
    ASSERT_FLOAT_EQ(tableRecip, expectedRecip);

    // Test index computation for all valid byte offsets within a page
    for (size_t j = 0; j <= getPageSize(); j += 8) {
      const size_t tableOff = float_recip::computeIndex(j, i);
      const size_t directOff = j / objectSize;
      ASSERT_EQ(tableOff, directOff) << "Mismatch at sizeClass=" << i << " offset=" << j
                                     << " objectSize=" << objectSize;
    }
  }
}
