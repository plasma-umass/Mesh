// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

#include "common.h"
#include "global_heap.h"
#include "mini_heap.h"
#include "runtime.h"
#include "memory_stats.h"
#include "fixed_array.h"
#include "internal.h"
#include "meshing.h"

namespace mesh {

inline bool ciDebugEnabled() {
  return getenv("CI_DEBUG_MESH") != nullptr;
}

#ifdef __linux__
uint64_t getArenaRssBytes(const void *arenaBegin) {
  FILE *fp = fopen("/proc/self/smaps", "r");
  if (!fp) {
    return 0;
  }

  const uintptr_t arenaStart = reinterpret_cast<uintptr_t>(arenaBegin);
  const uintptr_t arenaEnd = arenaStart + kArenaSize;

  uint64_t rssTotal = 0;
  bool inEntry = false;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    unsigned long start = 0;
    unsigned long end = 0;
    if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
      inEntry = (arenaStart < end) && (arenaEnd > start);
      continue;
    }

    if (!inEntry) {
      continue;
    }

    if (strncmp(line, "Rss:", 4) == 0) {
      unsigned long rssKb = 0;
      if (sscanf(line + 4, "%lu", &rssKb) == 1) {
        rssTotal += rssKb * 1024;
      }
    }
  }

  fclose(fp);
  return rssTotal;
}

void logSmapsForAddress(void *addr, const char *label) {
  if (!ciDebugEnabled()) {
    return;
  }

  FILE *fp = fopen("/proc/self/smaps", "r");
  if (!fp) {
    printf("[CI_DEBUG_MESH] Failed to open /proc/self/smaps for %s\n", label);
    return;
  }

  const uintptr_t target = reinterpret_cast<uintptr_t>(addr);
  bool in_entry = false;

  printf("[CI_DEBUG_MESH] /proc/self/smaps entry for %p (%s):\n", addr, label);

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    unsigned long start = 0;
    unsigned long end = 0;
    if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
      if (in_entry) {
        break;  // reached the next mapping
      }
      in_entry = target >= start && target < end;
    }

    if (in_entry) {
      printf("[CI_DEBUG_MESH]   %s", line);
    }
  }

  if (!in_entry) {
    printf("[CI_DEBUG_MESH]   (no smaps entry found for %p)\n", addr);
  }

  fclose(fp);
}
#endif

template <size_t PageSize>
void testPrecisePageDeallocation() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  // Initialize the runtime first - this creates MeshableArena which prints messages
  GlobalHeap<PageSize>& gheap = runtime<PageSize>().heap();
  const auto tid = gettid();
  const size_t pageSize = PageSize;  // 16KB on Apple Silicon, 4KB on x86

  // Disable automatic meshing for controlled test
  gheap.setMeshPeriodMs(kZeroMs);

  // Pre-initialize internal heap and allocatedBitmap to avoid RSS spikes
  // The allocatedBitmap() function allocates memory via internal::Heap().malloc()
  // which can grab ~1MB from the OS on first use
  {
    // Warm up by doing a dummy allocation and scavenge
    FixedArray<MiniHeap<PageSize>, 1> dummy_array{};
    gheap.allocSmallMiniheaps(SizeMap::SizeClass(256), 256, dummy_array, tid);
    MiniHeap<PageSize>* dummy_mh = dummy_array[0];
    if (dummy_mh) {
      void* dummy_ptr = dummy_mh->mallocAt(gheap.arenaBegin(), 0);
      if (dummy_ptr) {
        gheap.free(dummy_ptr);
      }
      gheap.freeMiniheap(dummy_mh);
    }
    // Trigger scavenge to initialize allocatedBitmap's internal heap
    gheap.scavenge(true);
  }


  printf("\n=== Testing Precise Page Deallocation ===\n");
  printf("Page size: %zu bytes\n", pageSize);
  printf("Platform: %s\n",
#ifdef __linux__
         "Linux"
#elif defined(__APPLE__)
         "macOS"
#else
         "Unknown"
#endif
  );

  // Get baseline memory stats AFTER warm-up to avoid measuring initialization
  MemoryStats baseline;
  ASSERT_TRUE(MemoryStats::get(baseline));
  printf("Baseline RSS: %" PRIu64 " bytes, Mesh memory: %" PRIu64 " bytes\n",
         baseline.resident_size_bytes, baseline.mesh_memory_bytes);

  // Phase 1: Allocate exactly 2 pages worth of objects in 2 miniheaps
  // Use small objects to fill the pages
  const size_t objectSize = 256;  // Small object size
  const size_t objectsPerPage = pageSize / objectSize;

  printf("\n=== Phase 1: Allocating 2 miniheaps with complementary allocation patterns ===\n");

  // Create first miniheap and allocate objects at specific offsets
  FixedArray<MiniHeap<PageSize>, 1> array1{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(objectSize), objectSize, array1, tid);
  MiniHeap<PageSize>* mh1 = array1[0];
  ASSERT_NE(mh1, nullptr);

  // Create second miniheap
  FixedArray<MiniHeap<PageSize>, 1> array2{};
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(objectSize), objectSize, array2, tid);
  MiniHeap<PageSize>* mh2 = array2[0];
  ASSERT_NE(mh2, nullptr);

  // Determine the exact region covering both spans.
  const size_t span1_offset = mh1->span().offset;
  const size_t span2_offset = mh2->span().offset;
  const size_t span1_pages = mh1->span().length;
  const size_t span2_pages = mh2->span().length;
  const size_t region_start_off = std::min(span1_offset, span2_offset);
  const size_t region_end_off = std::max(span1_offset + span1_pages, span2_offset + span2_pages);
  const size_t region_size = (region_end_off - region_start_off) * pageSize;
  const char* region_begin = gheap.arenaBegin() + region_start_off * pageSize;

  uint64_t baseline_region_bytes = 0;
  ASSERT_TRUE(MemoryStats::regionResidentBytes(region_begin, region_size, baseline_region_bytes));
  if (ciDebugEnabled()) {
    printf("[CI_DEBUG_MESH] Baseline resident bytes for span window [%zu, %zu): %" PRIu64 "\n",
           region_start_off, region_end_off, baseline_region_bytes);
  }

  // Allocate objects in complementary patterns so they can be meshed
  std::vector<void*> ptrs1, ptrs2;

  // We need at least 2 objects per miniheap to demonstrate meshing
  const size_t numObjects = std::min(objectsPerPage, static_cast<size_t>(16));

  // MiniHeap 1: Allocate at even indices
  for (size_t i = 0; i < numObjects; i += 2) {
    void* ptr = mh1->mallocAt(gheap.arenaBegin(), i);
    if (ptr) {
      ptrs1.push_back(ptr);
      memset(ptr, 0xAA, objectSize);  // Fill to ensure pages are allocated
    }
  }

  // MiniHeap 2: Allocate at odd indices
  for (size_t i = 1; i < numObjects; i += 2) {
    void* ptr = mh2->mallocAt(gheap.arenaBegin(), i);
    if (ptr) {
      ptrs2.push_back(ptr);
      memset(ptr, 0xBB, objectSize);  // Fill to ensure pages are allocated
    }
  }

  // Force pages to be resident
  for (auto ptr : ptrs1) {
    volatile char dummy = *reinterpret_cast<char*>(ptr);
    (void)dummy;
  }
  for (auto ptr : ptrs2) {
    volatile char dummy = *reinterpret_cast<char*>(ptr);
    (void)dummy;
  }

  MemoryStats after_alloc;
  ASSERT_TRUE(MemoryStats::get(after_alloc));

  uint64_t arena_bytes_after_alloc = 0;
  ASSERT_TRUE(MemoryStats::regionResidentBytes(region_begin, region_size, arena_bytes_after_alloc));
  const uint64_t arena_bytes_increase =
      arena_bytes_after_alloc > baseline_region_bytes ? arena_bytes_after_alloc - baseline_region_bytes : 0;
  if (ciDebugEnabled()) {
    printf("[CI_DEBUG_MESH] Resident bytes after allocation (window [%zu, %zu)): %" PRIu64 " (delta %" PRIu64 ")\n",
           region_start_off, region_end_off, arena_bytes_after_alloc, arena_bytes_increase);
  }
#ifdef __linux__
  logSmapsForAddress(gheap.arenaBegin(), "after phase 1 allocations");
#endif

  const size_t expected_allocated_pages = span1_pages + span2_pages;
  printf("\nAfter allocation: Mesh memory increased by %" PRIu64 " bytes (expected %zu bytes for %zu pages)\n",
         arena_bytes_increase, expected_allocated_pages * pageSize, expected_allocated_pages);

  EXPECT_EQ(arena_bytes_increase, expected_allocated_pages * pageSize);

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
  // After meshing, mh2 is untracked and destroyed but the pointer is not nullified
  // We must not use mh2 after this point as it points to freed memory

  // The meshing should have freed one physical page
  // Now scavenge to ensure the page is returned to the OS
  gheap.scavenge(true);

  MemoryStats after_mesh;
  ASSERT_TRUE(MemoryStats::get(after_mesh));

  uint64_t arena_bytes_after_mesh = 0;
  ASSERT_TRUE(MemoryStats::regionResidentBytes(region_begin, region_size, arena_bytes_after_mesh));
  const uint64_t arena_bytes_freed =
      arena_bytes_after_alloc > arena_bytes_after_mesh ? arena_bytes_after_alloc - arena_bytes_after_mesh : 0;
  if (ciDebugEnabled()) {
    printf("[CI_DEBUG_MESH] Resident bytes after mesh (window [%zu, %zu)): %" PRIu64 " (freed %" PRIu64 ")\n",
           region_start_off, region_end_off, arena_bytes_after_mesh, arena_bytes_freed);
  }
#ifdef __linux__
  logSmapsForAddress(gheap.arenaBegin(), "after meshing + scavenge");
#endif

  const uint64_t effective_mesh_memory_freed = arena_bytes_freed;

  printf("\nAfter meshing: Mesh memory decreased by %" PRIu64 " bytes (expected %zu bytes)\n",
         effective_mesh_memory_freed, span2_pages * pageSize);

  // CRITICAL ASSERTION: Meshing should free exactly the pages from the second miniheap
  EXPECT_EQ(effective_mesh_memory_freed, span2_pages * pageSize)
    << "Meshing should free exactly the pages from the second miniheap";

  // Verify data integrity after meshing
  for (auto ptr : ptrs1) {
    ASSERT_EQ(*reinterpret_cast<uint8_t*>(ptr), 0xAA) << "Data corruption in mh1";
  }
  for (auto ptr : ptrs2) {
    ASSERT_EQ(*reinterpret_cast<uint8_t*>(ptr), 0xBB) << "Data corruption in mh2";
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
  // Note: mh2 was already freed during meshLocked (untracked and destroyed)
  // Do NOT free mh2 here as it would be a double-free

  // Force a thorough cleanup to ensure memory is returned
  gheap.scavenge(true);

  MemoryStats final_stats;
  ASSERT_TRUE(MemoryStats::get(final_stats));

  uint64_t final_arena_bytes = 0;
  ASSERT_TRUE(MemoryStats::regionResidentBytes(region_begin, region_size, final_arena_bytes));
  const uint64_t final_arena_overhead =
      final_arena_bytes > baseline_region_bytes ? final_arena_bytes - baseline_region_bytes : 0;
  if (ciDebugEnabled()) {
    printf("[CI_DEBUG_MESH] Final resident bytes (window [%zu, %zu)): %" PRIu64 " (overhead %" PRIu64 ")\n",
           region_start_off, region_end_off, final_arena_bytes, final_arena_overhead);
  }

  const uint64_t effective_final_overhead = final_arena_overhead;

  printf("\nFinal mesh memory overhead: %" PRIu64 " bytes\n", effective_final_overhead);

  // Should return exactly to baseline within the measured region
  EXPECT_EQ(effective_final_overhead, 0u) << "Mesh memory should return to baseline in measured region";
}

template <size_t PageSize>
void testMemoryReductionAfterFree() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  // Direct test of memory release through free/scavenge
  GlobalHeap<PageSize>& gheap = runtime<PageSize>().heap();

  printf("\n=== Testing Memory Release After Free ===\n");

  // Get initial memory stats
  MemoryStats before;
  ASSERT_TRUE(MemoryStats::get(before));

  // Allocate a large block to ensure we have physical pages
  const size_t blockSize = 1024 * 1024;  // 1MB
  void* ptr = gheap.malloc(blockSize);
  ASSERT_NE(ptr, nullptr);

  // Fill with data to ensure pages are allocated
  memset(ptr, 0xFF, blockSize);

  // Force pages to be resident
  volatile char dummy = *reinterpret_cast<char*>(ptr);
  (void)dummy;

  MemoryStats after_alloc;
  ASSERT_TRUE(MemoryStats::get(after_alloc));

  printf("Mesh memory after allocation: %" PRIu64 " KB\n", after_alloc.mesh_memory_bytes / 1024);

  // Should have increased mesh memory
  EXPECT_GT(after_alloc.mesh_memory_bytes, before.mesh_memory_bytes);

  gheap.free(ptr);
  gheap.scavenge(true);

  MemoryStats after_free;
  ASSERT_TRUE(MemoryStats::get(after_free));

  printf("Mesh memory after free+scavenge: %" PRIu64 " KB\n", after_free.mesh_memory_bytes / 1024);

  // Mesh memory should be reduced after freeing and scavenging
  EXPECT_LT(after_free.mesh_memory_bytes, after_alloc.mesh_memory_bytes);
}

// Test wrappers that instantiate the correct template based on page size
TEST(MeshMemory, PrecisePageDeallocation) {
  const size_t pageSize = getPageSize();
  if (pageSize == 4096) {
    testPrecisePageDeallocation<4096>();
  } else if (pageSize == 16384) {
    testPrecisePageDeallocation<16384>();
  } else {
    GTEST_SKIP() << "Unsupported page size: " << pageSize;
  }
}

TEST(MeshMemory, VerifyMemoryReductionAfterFree) {
  const size_t pageSize = getPageSize();
  if (pageSize == 4096) {
    testMemoryReductionAfterFree<4096>();
  } else if (pageSize == 16384) {
    testMemoryReductionAfterFree<16384>();
  } else {
    GTEST_SKIP() << "Unsupported page size: " << pageSize;
  }
}

TEST(MeshMemory, BasicMemoryStats) {
  // Basic test to verify memory stats are working
  MemoryStats stats;
  ASSERT_TRUE(MemoryStats::get(stats));

  printf("\n=== Basic Memory Statistics ===\n");
  printf("Resident Size (RSS): %.2f MB\n",
         stats.resident_size_bytes / (1024.0 * 1024.0));

  // Just verify we got valid values
  EXPECT_GT(stats.resident_size_bytes, 0);

  // RSS should be at least a few MB for any running process
  EXPECT_GT(stats.resident_size_bytes, 1024 * 1024);
}

}  // namespace mesh
