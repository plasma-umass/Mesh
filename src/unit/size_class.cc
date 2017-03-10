// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdint.h>
#include <stdlib.h>

#include "gtest/gtest.h"

#include "common.h"

using namespace mesh;

#define roundtrip(n) ASSERT_TRUE(n == class2Size(size2Class(n)))
#define rt_debug(n) debug("%d c2s: %zu s2c: %d", n, class2Size(size2Class(n)), size2Class(n))

TEST(SizeClass, MinObjectSize) {
  ASSERT_EQ(MinObjectSize, 8);

  ASSERT_EQ(staticlog(MinObjectSize), 3);
}

TEST(SizeClass, SmallClasses) {
  roundtrip(16);
  // rt_debug(16);
  ASSERT_TRUE(size2Class(8) >= 0);

  roundtrip(8);
  // rt_debug(8);

  roundtrip(32);
}
