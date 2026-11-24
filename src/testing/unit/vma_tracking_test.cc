// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// The mesh cap tracks how many kernel mappings (VMAs) the arena occupies
// (MeshableArena::retrackVmaBoundaries).  Check it against /proc/self/maps
// as spans are meshed -- adjacent ones (whose VMAs the kernel merges), in
// random order, and chains -- and then freed and reset.

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include "gtest/gtest.h"

#include "internal.h"
#include "meshing.h"
#include "runtime.h"

using namespace mesh;

namespace {

constexpr uint32_t StrLen = 128;

// VMAs of this process overlapping [begin, end), from /proc/self/maps
size_t mappingsIn(const void *begin, const void *end) {
  FILE *f = fopen("/proc/self/maps", "r");
  if (f == nullptr) {
    return 0;
  }
  const uintptr_t lo = reinterpret_cast<uintptr_t>(begin), hi = reinterpret_cast<uintptr_t>(end);
  size_t n = 0;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    unsigned long a = 0, b = 0;
    if (sscanf(line, "%lx-%lx", &a, &b) == 2 && a < hi && b > lo) {
      n++;
    }
  }
  fclose(f);
  return n;
}

template <size_t PageSize>
struct Pair {
  MiniHeap<PageSize> *keep;
  MiniHeap<PageSize> *remove;
  void *keepObj;
  void *removeObj;
};

template <size_t PageSize>
MiniHeap<PageSize> *oneMiniheap(GlobalHeap<PageSize> &gheap) {
  FixedArray<MiniHeap<PageSize>, 1> array{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, gettid());
  MiniHeap<PageSize> *mh = array[0];
  array.clear();
  return mh;
}

// thp: as in THP mode, the arena is MADV_HUGEPAGE and remapped ranges are
// left unadvised, so they don't merge back with advised neighbors
template <size_t PageSize>
void vmaTrackingImpl(bool thp) {
#ifndef __linux__
  GTEST_SKIP() << "VMA tracking is Linux-only";
#endif
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &gheap = runtime<PageSize>().heap();
  gheap.setMeshPeriodMs(kZeroMs);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
  gheap.scavenge(true);
  if (thp) {
    gheap.setThpForTesting(true);
    gheap.adviseHugePages();
  }

  // exact whenever every meshed span's owner is known; otherwise an upper bound
  auto check = [&](const char *when, bool exact = true) {
    const size_t actual = mappingsIn(gheap.arenaBegin(), gheap.arenaEnd());
    const size_t tracked = gheap.arenaVmaCount();
    if (exact) {
      EXPECT_EQ(tracked, actual) << when;
    } else {
      EXPECT_GE(tracked, actual) << when;
    }
    return actual;
  };
  check("initially");

  const uint32_t objCount = std::min(static_cast<uint32_t>(PageSize / StrLen), 1024U);
  constexpr size_t kAdjacent = 64, kRandom = 64;

  // keeps allocated back to back, then removes back to back: meshing r_i onto
  // k_i in order lets the kernel merge the remapped VMAs
  std::vector<Pair<PageSize>> pairs;
  std::vector<MiniHeap<PageSize> *> keeps, removes;
  for (size_t i = 0; i < kAdjacent + kRandom; i++)
    keeps.push_back(oneMiniheap(gheap));
  for (size_t i = 0; i < kAdjacent + kRandom; i++)
    removes.push_back(oneMiniheap(gheap));
  for (size_t i = 0; i < keeps.size(); i++) {
    // pair kAdjacent+1 uses other offsets so it can later mesh with pair kAdjacent
    const uint32_t k = i == kAdjacent + 1 ? 1 : 0, r = i == kAdjacent + 1 ? objCount - 2 : objCount - 1;
    Pair<PageSize> p{keeps[i], removes[i], keeps[i]->mallocAt(gheap.arenaBegin(), k),
                     removes[i]->mallocAt(gheap.arenaBegin(), r)};
    pairs.push_back(p);
  }
  check("after allocation");

  for (size_t i = 0; i < kAdjacent; i++) {
    gheap.meshLocked(pairs[i].keep, pairs[i].remove);
  }
  const size_t afterAdjacent = check("after meshing adjacent spans in order");
  // merged: far fewer than one VMA (let alone two) per meshed span
  EXPECT_LT(afterAdjacent, 1 + kAdjacent);

  std::mt19937 rng(42);
  std::vector<size_t> order;
  for (size_t i = kAdjacent; i < pairs.size(); i++)
    order.push_back(i);
  std::shuffle(order.begin(), order.end(), rng);
  for (size_t i : order) {
    gheap.meshLocked(pairs[i].keep, pairs[i].remove);
  }
  check("after meshing in random order");

  // a chain: mesh one meshed pair (2 virtual spans) into another
  {
    auto &a = pairs[kAdjacent], &b = pairs[kAdjacent + 1];
    ASSERT_TRUE(mesh::bitmapsMeshable(a.keep->bitmap().bits(), b.keep->bitmap().bits(), a.keep->bitmap().byteCount()));
    auto src = b.keep;
    gheap.meshLocked(a.keep, src);
    EXPECT_EQ(a.keep->meshCount(), 4UL);
    check("after meshing a chain");
    b.keep = nullptr;  // part of a's chain now
  }

  // the cap: meshing a chain of `spans` spans needs 2*spans VMAs of headroom
  // beyond the reserve
  {
    constexpr size_t limit = 1 << 16;
    gheap.setMaxMapCount(limit);
    const size_t headroom = limit - limit / kMapCountReserveDivisor - gheap.arenaVmaCount();
    EXPECT_FALSE(gheap.meshWouldExceedMapLimit(headroom / 2));
    EXPECT_TRUE(gheap.meshWouldExceedMapLimit(headroom / 2 + 1));
    EXPECT_EQ(gheap.arenaVmaLimit(), limit - limit / kMapCountReserveDivisor);
    gheap.setMaxMapCount(0);  // unit tests run without a map-count cap
  }

  // free everything; resets restore identity mappings and the VMAs merge back
  for (auto &p : pairs) {
    gheap.free(p.keepObj);
    gheap.free(p.removeObj);
  }
  for (auto &p : pairs) {
    if (p.keep != nullptr && p.keep->isEmpty()) {
      auto mh = p.keep;
      gheap.freeMiniheap(mh);
    }
  }
  check("after freeing (resets pending)", false);  // owners gone: may over-count
  gheap.scavenge(true);
  const size_t after = check("after scavenge");
  if (!gheap.hugePagesAdvised()) {
    EXPECT_EQ(after, 1UL);  // (advised: reset ranges stay separate VMAs)
  }
  EXPECT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
  gheap.setThpForTesting(false);
}

}  // namespace

TEST(VmaTrackingTest, MatchesProcMaps) {
  if (getPageSize() == 4096) {
    vmaTrackingImpl<4096>(false);
  } else {
    vmaTrackingImpl<16384>(false);
  }
}

// (after the test above: once advised, the arena stays advised)
TEST(VmaTrackingTest, MatchesProcMapsThp) {
  if (getPageSize() == 4096) {
    vmaTrackingImpl<4096>(true);
  } else {
    vmaTrackingImpl<16384>(true);
  }
}
