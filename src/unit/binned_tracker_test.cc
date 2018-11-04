// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <cstdint>
#include <cstdlib>

#include "binned_tracker.h"

#include "gtest/gtest.h"


using namespace mesh;
using namespace mesh::internal;

TEST(BinnedTracker, Tests) {
  const BinToken token;

  ASSERT_FALSE(token.valid());

  ASSERT_NE(bintoken::FlagFull, bintoken::FlagEmpty);

  ASSERT_FALSE(BinToken::Full().valid());

  ASSERT_TRUE(BinToken::Full().newOff(2).valid());
  ASSERT_TRUE(BinToken::Full().newOff(0).valid());
}
