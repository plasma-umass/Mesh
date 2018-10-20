// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdalign.h>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

#include "rng/mwc.h"

using namespace mesh;

TEST(RNG, MWCRange) {
  MWC mwc{internal::seed(), internal::seed()};
  for (size_t i = 0; i < 1000; i++) {
    size_t n = mwc.inRange(0, 1);
    if (n != 0 && n != 1) {
      ASSERT_TRUE(false);
    }
  }
}
