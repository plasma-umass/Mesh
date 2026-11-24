// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// THP mode: zone separation of spans by size class, and the per-region
// intact/split and fill bookkeeping behind the protect rule.

#include <stdint.h>

#include <vector>

#include "gtest/gtest.h"

#include "internal.h"
#include "runtime.h"

using namespace mesh;

namespace {

template <size_t PageSize>
MiniHeap<PageSize> *oneMiniheap(GlobalHeap<PageSize> &gheap, size_t objectSize) {
  FixedArray<MiniHeap<PageSize>, 1> array{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(objectSize), objectSize, array, gettid());
  MiniHeap<PageSize> *mh = array[0];
  array.clear();
  return mh;
}

// a 512 B miniheap that is the first in its region (earlier tests may have
// left free pages in touched regions; those get used up into fillers)
template <size_t PageSize>
MiniHeap<PageSize> *firstInFreshRegion(GlobalHeap<PageSize> &gheap, std::vector<MiniHeap<PageSize> *> &fillers) {
  while (true) {
    const size_t touched = gheap.thpStats().regions;
    auto mh = oneMiniheap(gheap, 512);
    if (gheap.thpStats().regions > touched)
      return mh;
    fillers.push_back(mh);
  }
}

template <size_t PageSize>
void thpImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &gheap = runtime<PageSize>().heap();
  gheap.setMeshPeriodMs(kZeroMs);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
  constexpr size_t kRegionPages = kThpRegionBytes / PageSize;
  auto region = [&](MiniHeap<PageSize> *mh) { return mh->span().offset / kRegionPages; };

  // THP off: one zone (THP mode may be on by default on this kernel)
  gheap.setThpForTesting(false);
  {
    auto small = oneMiniheap(gheap, 64), big = oneMiniheap(gheap, 512);
    EXPECT_EQ(gheap.zoneOf(small->span().offset), 0);
    EXPECT_EQ(gheap.zoneOf(big->span().offset), 0);
    gheap.freeMiniheap(small);
    gheap.freeMiniheap(big);
    gheap.scavenge(true);
  }

  gheap.setThpForTesting(true, 0.9);
  // 512 B+ classes go to zone-1 regions, small classes to zone 0
  auto small = oneMiniheap(gheap, 64);
  std::vector<MiniHeap<PageSize> *> fillers, bigs;
  bigs.push_back(firstInFreshRegion(gheap, fillers));
  EXPECT_EQ(gheap.zoneOf(small->span().offset), 0);
  EXPECT_EQ(gheap.zoneOf(bigs[0]->span().offset), 1);
  EXPECT_NE(region(small), region(bigs[0]));

  // fill the big span's region past 90%: dense and intact
  const size_t r = region(bigs[0]);
  while (true) {
    auto mh = oneMiniheap(gheap, 512);
    if (region(mh) != r) {  // spilled into the next region
      gheap.freeMiniheap(mh);
      break;
    }
    bigs.push_back(mh);
  }
  gheap.scavenge(true);  // release the spilled span now, so only the victim below splits a region
  EXPECT_GE(bigs.size(), static_cast<size_t>(0.9 * kRegionPages));
  EXPECT_TRUE(gheap.spanInDenseIntactRegion(bigs[0]->span()));
  EXPECT_FALSE(gheap.spanInDenseIntactRegion(small->span()));  // its region is nearly empty
  const auto before = gheap.thpStats();
  EXPECT_GE(before.intact, 1UL);  // the big span's region (the small span may reuse split pages)

  // free one span and release its pages: the region's huge page splits
  auto victim = bigs.back();
  bigs.pop_back();
  gheap.freeMiniheap(victim);
  gheap.scavenge(true);
  EXPECT_FALSE(gheap.spanInDenseIntactRegion(bigs[0]->span()));
  EXPECT_EQ(gheap.thpStats().split, before.split + 1);

  for (auto mh : bigs) {
    gheap.freeMiniheap(mh);
  }
  for (auto mh : fillers) {
    gheap.freeMiniheap(mh);
  }
  gheap.freeMiniheap(small);
  gheap.scavenge(true);
  gheap.setThpForTesting(false);
  EXPECT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
}

}  // namespace

TEST(ThpTest, ZonesAndRegionState) {
  if (getPageSize() == 4096) {
    thpImpl<4096>();
  } else {
    thpImpl<16384>();
  }
}
