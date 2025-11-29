// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <cstdint>

#include "gtest/gtest.h"

#include "global_heap.h"

using namespace mesh;

TEST(CachelinePadding, PendingPartialHeadSize) {
  // CachelinePaddedAtomicMiniHeapID must be exactly one cache line
  ASSERT_EQ(sizeof(CachelinePaddedAtomicMiniHeapID), CACHELINE_SIZE);
}

TEST(CachelinePadding, PendingPartialHeadAlignment) {
  // CachelinePaddedAtomicMiniHeapID must be cache-line aligned
  ASSERT_EQ(alignof(CachelinePaddedAtomicMiniHeapID), CACHELINE_SIZE);
}

TEST(CachelinePadding, PendingPartialHeadArraySize) {
  // The array should be kNumBins * CACHELINE_SIZE bytes
  ASSERT_EQ(sizeof(std::array<CachelinePaddedAtomicMiniHeapID, kNumBins>), kNumBins * CACHELINE_SIZE);
}

TEST(CachelinePadding, PendingPartialHeadArrayElements) {
  // Verify that array elements are properly spaced
  std::array<CachelinePaddedAtomicMiniHeapID, kNumBins> arr{};

  for (size_t i = 0; i < kNumBins; i++) {
    // Each element should be at offset i * CACHELINE_SIZE from the start
    uintptr_t base = reinterpret_cast<uintptr_t>(&arr[0]);
    uintptr_t elem = reinterpret_cast<uintptr_t>(&arr[i]);
    ASSERT_EQ(elem - base, i * CACHELINE_SIZE) << "Element " << i << " is not at expected offset";

    // Each element should be cache-line aligned
    ASSERT_EQ(elem % CACHELINE_SIZE, 0) << "Element " << i << " is not cache-line aligned";
  }
}

TEST(CachelinePadding, AtomicHeadMemberOffset) {
  // The 'head' member should be at offset 0 within the struct
  CachelinePaddedAtomicMiniHeapID padded{};
  ASSERT_EQ(reinterpret_cast<uintptr_t>(&padded.head), reinterpret_cast<uintptr_t>(&padded));
}
