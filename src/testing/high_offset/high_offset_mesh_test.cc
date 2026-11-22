// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 University of Massachusetts, Amherst

// End-to-end regression test for spans that live more than 4 GiB into the
// arena.  Page offsets are 32 bits, and an earlier version of Mesh computed
// backing-file offsets as `offset << kPageShift` in 32 bits, so meshing or
// resetting a span above 4 GiB silently mapped the wrong part of the backing
// file.
//
// This lives in its own binary (not //src:unit-tests) because it reserves a
// 4 GiB placeholder span to push later spans above the 4 GiB mark.  The
// placeholder is never touched, so it costs address space but almost no RSS.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "gtest/gtest.h"

#include "internal.h"
#include "meshing.h"
#include "runtime.h"

using namespace mesh;

static constexpr uint32_t StrLen = 128;
static constexpr size_t k4GiB = 1ULL << 32;

template <size_t PageSize>
static void highOffsetMeshImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  const uint32_t ObjCount = std::min(static_cast<uint32_t>(PageSize / StrLen), 1024U);
  const size_t pagesIn4GiB = k4GiB / PageSize;

  const auto tid = gettid();
  GlobalHeap<PageSize> &gheap = runtime<PageSize>().heap();
  char *const arenaBegin = reinterpret_cast<char *>(gheap.arenaBegin());

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);

  // reserve (but never touch) 4 GiB so the spans below are allocated past it
  char *pad = reinterpret_cast<char *>(gheap.malloc(k4GiB));
  ASSERT_TRUE(pad != nullptr);
  const size_t padOff = static_cast<size_t>(pad - arenaBegin) / PageSize;

  FixedArray<MiniHeap<PageSize>, 1> array{};

  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap<PageSize> *mh1 = array[0];
  array.clear();

  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap<PageSize> *mh2 = array[0];
  array.clear();

  ASSERT_TRUE(mh1 != mh2);
  ASSERT_EQ(mh1->maxCount(), ObjCount);
  ASSERT_EQ(mh2->maxCount(), ObjCount);

  // the whole point: both spans start more than 4 GiB into the backing file
  const Span keepSpan = mh1->span();
  const Span removeSpan = mh2->span();
  ASSERT_GE(keepSpan.offset, pagesIn4GiB);
  ASSERT_GE(removeSpan.offset, pagesIn4GiB);

  // where a truncated (offset mod 4 GiB) mapping would land; it must fall
  // inside the untouched placeholder for the checks below to be meaningful
  const size_t removeAliasOff = removeSpan.offset - pagesIn4GiB;
  ASSERT_GE(removeAliasOff, padOff);
  ASSERT_LT(removeAliasOff + removeSpan.length, padOff + pagesIn4GiB);

  char *s1 = reinterpret_cast<char *>(mh1->mallocAt(gheap.arenaBegin(), 0));
  char *s2 = reinterpret_cast<char *>(mh2->mallocAt(gheap.arenaBegin(), ObjCount - 1));
  ASSERT_TRUE(s1 != nullptr);
  ASSERT_TRUE(s2 != nullptr);

  memset(s1, 'A', StrLen);
  memset(s2, 'Z', StrLen);
  s1[StrLen - 1] = 0;
  s2[StrLen - 1] = 0;

  char *v1 = strdup(s1);
  char *v2 = strdup(s2);

  // mesh mh2 into mh1: mh2's virtual span is remapped onto mh1's file pages
  gheap.meshLocked(mh1, mh2);
  ASSERT_EQ(mh1->inUseCount(), 2UL);
  ASSERT_EQ(mh1->meshCount(), 2ULL);

  // with a truncated file offset, s2's span maps a hole in the placeholder
  // and reads back as zeros
  ASSERT_STREQ(s1, v1);
  ASSERT_STREQ(s2, v2);

  // both virtual spans share one physical span
  char *s3 = s1 + (ObjCount - 1) * StrLen;
  ASSERT_STREQ(s2, s3);
  s2[0] = 'b';
  ASSERT_EQ(s3[0], 'b');

  gheap.free(s1);
  gheap.free(s2);
  ASSERT_TRUE(mh1->isEmpty());
  gheap.freeMiniheap(mh1);

  // scavenging restores the identity mapping for the formerly meshed span
  gheap.scavenge(true);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 1UL);  // just the placeholder

  // after the reset, writes to the old span must not show up in the
  // placeholder (they would if the reset used a truncated offset)
  char *removeStart = arenaBegin + static_cast<size_t>(removeSpan.offset) * PageSize;
  char *aliasStart = arenaBegin + removeAliasOff * PageSize;
  removeStart[0] = 'M';
  EXPECT_EQ(aliasStart[0], 0);
  removeStart[0] = 0;

  free(v1);
  free(v2);

  gheap.free(pad);
  gheap.scavenge(true);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
}

TEST(HighOffsetMeshTest, MeshAndResetAbove4GiB) {
  if (getPageSize() == 4096) {
    highOffsetMeshImpl<4096>();
  } else {
    highOffsetMeshImpl<16384>();
  }
}
