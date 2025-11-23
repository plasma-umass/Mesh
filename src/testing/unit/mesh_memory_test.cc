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

#include "common.h"
#include "global_heap.h"
#include "mini_heap.h"
#include "runtime.h"
#include "memory_stats.h"
#include "fixed_array.h"
#include "internal.h"
#include "meshing.h"

namespace mesh {

#ifdef __linux__
bool ciDebugEnabled() {
  return getenv("CI_DEBUG_MESH") != nullptr;
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

#ifdef __linux__
  // On Linux, we need RssShmem tracking to work properly for this test.
  // Some CI environments or containers might not support this properly.
  // Check if RssShmem tracking is working by looking at the current process.
  {
    printf("\n=== Checking RssShmem tracking availability ===\n");

    // First check baseline RssShmem before any test allocation
    MemoryStats baseline_check;
    bool baseline_success = MemoryStats::get(baseline_check);
    printf("Baseline MemoryStats::get() returned: %s\n", baseline_success ? "true" : "false");
    if (baseline_success) {
      printf("Baseline mesh_memory_bytes before test allocation: %" PRIu64 " bytes\n",
             baseline_check.mesh_memory_bytes);
    }

    // Allocate a larger block to ensure RssShmem is properly tracked
    // Small allocations (256 bytes) don't always show up in RssShmem immediately
    // Use 1MB allocation like the VerifyMemoryReductionAfterFree test which works
    printf("Allocating 1MB test block to verify RssShmem tracking...\n");
    const size_t test_size = 1024 * 1024;  // 1MB
    void* large_test_ptr = gheap.malloc(test_size);

    if (large_test_ptr) {
      printf("Test allocation returned: %p\n", large_test_ptr);

      // Fill with data to ensure pages are allocated
      memset(large_test_ptr, 0x42, test_size);

      // Force pages to be resident
      for (size_t i = 0; i < test_size; i += 4096) {
        volatile char dummy = *reinterpret_cast<char*>(
          reinterpret_cast<char*>(large_test_ptr) + i);
        (void)dummy;
      }
      printf("Successfully wrote to and read from 1MB test allocation\n");

      // Give time for kernel to update stats
      printf("Sleeping 100ms for kernel to update stats...\n");
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      MemoryStats test_stats;
      bool test_success = MemoryStats::get(test_stats);
      printf("After allocation MemoryStats::get() returned: %s\n", test_success ? "true" : "false");

      if (test_success) {
        printf("After allocation mesh_memory_bytes: %" PRIu64 " bytes\n",
               test_stats.mesh_memory_bytes);
        printf("Increase from baseline: %" PRIu64 " bytes\n",
               test_stats.mesh_memory_bytes > baseline_check.mesh_memory_bytes ?
               test_stats.mesh_memory_bytes - baseline_check.mesh_memory_bytes : 0);

        // If we allocated memory but mesh_memory_bytes is still 0, something is wrong
        if (test_stats.mesh_memory_bytes == 0) {
          printf("\n=== WARNING: mesh_memory_bytes is 0 after allocation ===\n");
          printf("This suggests one of the following:\n");
          printf("1. Memory allocation didn't use memfd_create path (most likely)\n");
          printf("2. RssShmem field exists but is 0 (pages not yet resident)\n");
          printf("3. Build configuration issue (CI_DEBUG_MESH not propagating)\n");
          printf("Check stderr output for [CI_DEBUG_MESH] messages.\n");
          printf("Continuing test anyway to see if it's a measurement issue...\n\n");
          // Don't skip - let's see if the test can still work
        } else {
          printf("RssShmem tracking appears to be working correctly (mesh_memory=%llu bytes).\n",
                 static_cast<unsigned long long>(test_stats.mesh_memory_bytes));
        }
      } else {
        printf("ERROR: MemoryStats::get() failed after allocation\n");
        printf("Check stderr output above for [MemoryStats] DEBUG messages.\n");
        printf("Continuing test anyway...\n\n");
        // Don't skip - let's see if the test can still work
      }

      // Clean up test allocation
      if (large_test_ptr) {
        gheap.free(large_test_ptr);
      }
    } else {
      printf("ERROR: Failed to allocate test memory block\n");
      printf("Continuing test anyway...\n\n");
      // Don't skip - let's see if the test can still work
    }

    // Force scavenge to clean up the test allocation before continuing
    gheap.scavenge(true);

    // Take a fresh baseline AFTER cleanup for accurate Phase 1 measurements
    ASSERT_TRUE(MemoryStats::get(baseline));
    printf("Post-cleanup baseline RSS: %" PRIu64 " bytes, Mesh memory: %" PRIu64 " bytes\n\n",
           baseline.resident_size_bytes, baseline.mesh_memory_bytes);
  }
#endif

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

  uint64_t mesh_memory_increase = after_alloc.mesh_memory_bytes - baseline.mesh_memory_bytes;

#ifdef __linux__
  logSmapsForAddress(gheap.arenaBegin(), "after phase 1 allocations");
#endif

  printf("\nAfter allocation: Mesh memory increased by %" PRIu64 " bytes (expected ~%zu bytes for 2 pages)\n",
         mesh_memory_increase, 2 * pageSize);

  // Verify we allocated approximately 2 pages worth of memory
  // The mesh_memory_bytes metric should closely match actual physical pages allocated
  EXPECT_GE(mesh_memory_increase, 2 * pageSize);
  EXPECT_LE(mesh_memory_increase, 3 * pageSize);  // Allow one extra page for metadata

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

#ifdef __linux__
  logSmapsForAddress(gheap.arenaBegin(), "after meshing + scavenge");
#endif

  // The critical metric: mesh_memory_bytes should decrease after meshing
  // Compare to after_alloc, not baseline, to avoid measuring internal allocations during scavenge
  uint64_t mesh_memory_freed = 0;
  if (after_mesh.mesh_memory_bytes < after_alloc.mesh_memory_bytes) {
    mesh_memory_freed = after_alloc.mesh_memory_bytes - after_mesh.mesh_memory_bytes;
    printf("\nAfter meshing: Mesh memory decreased by %" PRIu64 " bytes (expected ~%zu bytes)\n",
           mesh_memory_freed, pageSize);
  } else if (after_mesh.mesh_memory_bytes > after_alloc.mesh_memory_bytes) {
    // This should not happen - meshing should reduce memory
    printf("\nERROR: After meshing, mesh memory increased by %" PRIu64 " bytes (expected decrease of ~%zu bytes)\n",
           after_mesh.mesh_memory_bytes - after_alloc.mesh_memory_bytes, pageSize);
  } else {
    printf("\nAfter meshing: Mesh memory unchanged\n");
  }

  // CRITICAL ASSERTION: Meshing should free approximately one page of physical memory
  // This is the core test - verifying that meshing actually reduces physical memory usage
  EXPECT_GE(mesh_memory_freed, pageSize - (pageSize / 4))
    << "Meshing should free at least 75% of a page";
  EXPECT_LE(mesh_memory_freed, pageSize + (pageSize / 4))
    << "Meshing should free at most 125% of a page";

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

  uint64_t final_mesh_overhead = 0;
  if (final_stats.mesh_memory_bytes > baseline.mesh_memory_bytes) {
    final_mesh_overhead = final_stats.mesh_memory_bytes - baseline.mesh_memory_bytes;
  }
  printf("\nFinal mesh memory overhead: %" PRIu64 " bytes\n", final_mesh_overhead);

  // Should be back close to baseline (within 1MB tolerance for heap metadata)
  EXPECT_LT(final_mesh_overhead, 1024 * 1024) << "Mesh memory should return close to baseline";
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
