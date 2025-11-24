// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __APPLE__

#include <gtest/gtest.h>
#include <unistd.h>
#include <thread>
#include <chrono>

#include "common.h"
#include "global_heap.h"
#include "mini_heap.h"
#include "runtime.h"
#include "memory_stats_macos.h"
#include "fixed_array.h"
#include "internal.h"
#include "meshing.h"

namespace mesh {

static constexpr size_t PageSize = getPageSize();
using GlobalHeapType = GlobalHeap<PageSize>;
using MiniHeapType = MiniHeap<PageSize>;

TEST(MacOSMemory, PrecisePageDeallocationWithPunchhole) {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  GlobalHeapType &gheap = runtime<PageSize>().heap();
  const auto tid = gettid();
  const size_t pageSize = PageSize;  // 16KB on Apple Silicon, 4KB on Intel

  // Disable automatic meshing for controlled test
  gheap.setMeshPeriodMs(kZeroMs);

  printf("\n=== Testing Precise Page Deallocation ===\n");
  printf("Page size: %zu bytes\n", pageSize);
  printf("Note: Using RSS for MAP_SHARED memory tracking\n");

  // Get baseline memory stats
  MacOSMemoryStats baseline;
  ASSERT_TRUE(MacOSMemoryStats::get(baseline));
  printf("Baseline RSS: %llu bytes\n", baseline.resident_size);

  // Phase 1: Allocate exactly 2 pages worth of objects in 2 miniheaps
  // Use small objects to fill the pages
  const size_t objectSize = 256;  // Small object size
  const size_t objectsPerPage = pageSize / objectSize;

  printf("\n=== Phase 1: Allocating 2 miniheaps with complementary allocation patterns ===\n");

  // Create first miniheap and allocate objects at specific offsets
  FixedArray<MiniHeapType, 1> array1{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(objectSize), objectSize, array1, tid);
  MiniHeapType *mh1 = array1[0];
  ASSERT_NE(mh1, nullptr);

  // Create second miniheap
  FixedArray<MiniHeapType, 1> array2{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(objectSize), objectSize, array2, tid);
  MiniHeapType *mh2 = array2[0];
  ASSERT_NE(mh2, nullptr);

  // Allocate objects in complementary patterns so they can be meshed
  std::vector<void *> ptrs1, ptrs2;

  // We need at least 2 objects per miniheap to demonstrate meshing
  const size_t numObjects = std::min(objectsPerPage, static_cast<size_t>(16));

  // MiniHeap 1: Allocate at even indices
  for (size_t i = 0; i < numObjects; i += 2) {
    void *ptr = mh1->mallocAt(gheap.arenaBegin(), i);
    if (ptr) {
      ptrs1.push_back(ptr);
      memset(ptr, 0xAA, objectSize);  // Fill to ensure pages are allocated
    }
  }

  // MiniHeap 2: Allocate at odd indices
  for (size_t i = 1; i < numObjects; i += 2) {
    void *ptr = mh2->mallocAt(gheap.arenaBegin(), i);
    if (ptr) {
      ptrs2.push_back(ptr);
      memset(ptr, 0xBB, objectSize);  // Fill to ensure pages are allocated
    }
  }

  // Force pages to be resident
  for (auto ptr : ptrs1) {
    volatile char dummy = *reinterpret_cast<char *>(ptr);
    (void)dummy;
  }
  for (auto ptr : ptrs2) {
    volatile char dummy = *reinterpret_cast<char *>(ptr);
    (void)dummy;
  }

  // Let memory settle
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  MacOSMemoryStats after_alloc;
  ASSERT_TRUE(MacOSMemoryStats::get(after_alloc));

  uint64_t memory_increase = after_alloc.resident_size - baseline.resident_size;
  printf("\nAfter allocation: RSS increased by %llu bytes (expected ~%zu bytes for 2 pages)\n", memory_increase,
         2 * pageSize);

  // Verify we allocated approximately 2 pages worth of memory
  // Allow some overhead for metadata
  EXPECT_GE(memory_increase, 2 * pageSize);
  EXPECT_LE(memory_increase, 4 * pageSize);  // Allow more slack for RSS measurement

  // Phase 2: Verify the miniheaps are meshable
  printf("\n=== Phase 2: Verifying miniheaps are meshable ===\n");

  const auto bitmap1 = mh1->bitmap().bits();
  const auto bitmap2 = mh2->bitmap().bits();
  const auto len = mh1->bitmap().byteCount();
  ASSERT_EQ(len, mh2->bitmap().byteCount());

  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap1, bitmap2, len))
      << "Miniheaps should be meshable with complementary allocation patterns";

  // Phase 3: Mesh the miniheaps - this should free exactly 1 page
  printf("\n=== Phase 3: Meshing miniheaps (should free exactly %zu bytes) ===\n", pageSize);

  gheap.meshLocked(mh1, mh2);

  // The meshing should have called freePhys with F_PUNCHHOLE on the freed page
  // Now scavenge to ensure the page is returned
  gheap.scavenge(true);

  // Let OS reclaim the page
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  MacOSMemoryStats after_mesh;
  ASSERT_TRUE(MacOSMemoryStats::get(after_mesh));

  uint64_t memory_freed = after_alloc.resident_size - after_mesh.resident_size;
  printf("\nAfter meshing: RSS decreased by %llu bytes (expected %zu bytes)\n", memory_freed, pageSize);

  // CRITICAL ASSERTION: We should have freed exactly one page
  // Allow small tolerance for measurement precision (±2KB)
  EXPECT_GE(memory_freed, pageSize - 2048) << "Should free at least one page minus small tolerance";
  EXPECT_LE(memory_freed, pageSize + 2048) << "Should free at most one page plus small tolerance";

  // Verify data integrity after meshing
  for (auto ptr : ptrs1) {
    ASSERT_EQ(*reinterpret_cast<uint8_t *>(ptr), 0xAA) << "Data corruption in mh1";
  }
  for (auto ptr : ptrs2) {
    ASSERT_EQ(*reinterpret_cast<uint8_t *>(ptr), 0xBB) << "Data corruption in mh2";
  }

  // Phase 4: Clean up and verify return to baseline
  printf("\n=== Phase 4: Cleanup ===\n");

  for (auto ptr : ptrs1) {
    gheap.free(ptr);
  }
  for (auto ptr : ptrs2) {
    gheap.free(ptr);
  }

  gheap.freeMiniheap(mh1);
  gheap.scavenge(true);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  MacOSMemoryStats final_stats;
  ASSERT_TRUE(MacOSMemoryStats::get(final_stats));

  uint64_t final_overhead = final_stats.resident_size - baseline.resident_size;
  printf("\nFinal RSS overhead: %llu bytes\n", final_overhead);

  // Should be back close to baseline (within 1MB tolerance for heap metadata)
  EXPECT_LT(final_overhead, 1024 * 1024) << "RSS should return close to baseline";
}

TEST(MacOSMemory, VerifyPunchholeReducesFileSize) {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  // Direct test of F_PUNCHHOLE on our arena file
  GlobalHeapType &gheap = runtime<PageSize>().heap();

  printf("\n=== Testing F_PUNCHHOLE memory release ===\n");

  // Get initial memory stats
  MacOSMemoryStats before;
  ASSERT_TRUE(MacOSMemoryStats::get(before));

  // Allocate a large block to ensure we have file-backed pages
  const size_t blockSize = 1024 * 1024;  // 1MB
  void *ptr = gheap.malloc(blockSize);
  ASSERT_NE(ptr, nullptr);

  // Fill with data to ensure pages are allocated
  memset(ptr, 0xFF, blockSize);

  // Force pages to be resident
  volatile char dummy = *reinterpret_cast<char *>(ptr);
  (void)dummy;

  // Let memory settle
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  MacOSMemoryStats after_alloc;
  ASSERT_TRUE(MacOSMemoryStats::get(after_alloc));

  printf("RSS after allocation: %llu KB\n", after_alloc.resident_size / 1024);

  // Should have increased RSS
  EXPECT_GT(after_alloc.resident_size, before.resident_size);

  gheap.free(ptr);
  gheap.scavenge(true);

  // Let OS reclaim pages
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  MacOSMemoryStats after_free;
  ASSERT_TRUE(MacOSMemoryStats::get(after_free));

  printf("RSS after free+scavenge: %llu KB\n", after_free.resident_size / 1024);

  // The scavenge should have called freePhys which uses F_PUNCHHOLE
  // RSS should be reduced
  EXPECT_LT(after_free.resident_size, after_alloc.resident_size);
}

TEST(MacOSMemory, CompareFootprintVsRSS) {
  // This test documents the relationship between footprint and RSS with MAP_SHARED memory
  MacOSMemoryStats stats;
  ASSERT_TRUE(MacOSMemoryStats::get(stats));

  printf("\n=== Footprint vs RSS Comparison ===\n");
  printf("Physical Footprint: %.2f MB (excludes MAP_SHARED pages)\n", stats.physical_footprint / (1024.0 * 1024.0));
  printf("Resident Size (RSS): %.2f MB (includes MAP_SHARED pages)\n", stats.resident_size / (1024.0 * 1024.0));
  printf("Compressed Memory: %.2f MB\n", stats.compressed / (1024.0 * 1024.0));

  // With MAP_SHARED memory, RSS can be greater than footprint
  // This is expected behavior on macOS
  printf("Note: RSS may exceed footprint due to MAP_SHARED memory accounting\n");

  // Just verify we got valid values
  EXPECT_GT(stats.resident_size, 0);
  EXPECT_GT(stats.physical_footprint, 0);
}

}  // namespace mesh

#endif  // __APPLE__