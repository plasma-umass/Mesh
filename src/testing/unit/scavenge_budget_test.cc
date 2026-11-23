// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// A scavenge cut short by its time budget leaves the rest of the dirty spans
// queued (and off the clean lists) for the next scavenge.  In debug builds
// scavenge() also cross-checks its free-page bitmap, which must agree.

#include <stdint.h>

#include <vector>

#include "gtest/gtest.h"

#include "internal.h"
#include "runtime.h"

using namespace mesh;

namespace {

template <size_t PageSize>
void scavengeBudgetImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &gheap = runtime<PageSize>().heap();
  gheap.setMeshPeriodMs(kZeroMs);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
  gheap.scavenge(true);
  ASSERT_EQ(gheap.dirtyPageCount(), 0UL);

  // free enough miniheaps to leave many dirty spans
  constexpr size_t kMiniheaps = 256;
  std::vector<MiniHeap<PageSize> *> mhs;
  for (size_t i = 0; i < kMiniheaps; i++) {
    FixedArray<MiniHeap<PageSize>, 1> array{};
    gheap.allocSmallMiniheaps(SizeMap::SizeClass(64), 64, array, gettid());
    mhs.push_back(array[0]);
    array.clear();
  }
  for (auto mh : mhs) {
    gheap.freeMiniheap(mh);
  }
  const size_t dirty = gheap.dirtyPageCount();
  ASSERT_GE(dirty, kMiniheaps);

  // a 1 ns budget: nothing gets released, everything stays queued
  gheap.setScavengeBudgetNs(1);
  gheap.scavenge(true);
  EXPECT_EQ(gheap.dirtyPageCount(), dirty);
  gheap.partialScavenge();
  EXPECT_EQ(gheap.dirtyPageCount(), dirty);

  // unbounded: all of it goes
  gheap.setScavengeBudgetNs(0);
  gheap.scavenge(true);
  EXPECT_EQ(gheap.dirtyPageCount(), 0UL);
  EXPECT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);

  gheap.setScavengeBudgetNs(
      static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(kScavengeBudget).count()));
}

}  // namespace

TEST(ScavengeBudgetTest, LeftoversStayQueued) {
  if (getPageSize() == 4096) {
    scavengeBudgetImpl<4096>();
  } else {
    scavengeBudgetImpl<16384>();
  }
}
