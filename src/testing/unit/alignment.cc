// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdalign.h>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

#include "common.h"
#include "internal.h"
#include "thread_local_heap.h"

using namespace mesh;

template <size_t PageSize>
void TestNaturalAlignment() {
  auto heap = ThreadLocalHeap<PageSize>::GetHeap();

  void **ptrs = reinterpret_cast<void **>(calloc(256, sizeof(void *)));
  for (size_t size = 0; size < 4096; size += 4) {
    for (size_t alignment = 2; alignment <= 4096; alignment *= 2) {
      // debug("size: %zu align: %zu\n", size, alignment);
      bool logged = false;
      for (size_t i = 0; i < 256; i++) {
        void *ptr = heap->memalign(alignment, size);
        if (!logged) {
          size_t actual = heap->getSize(ptr);
          // debug("%10zu %10zu %10zu %10p\n", size, actual, alignment, ptr);
          logged = true;
        }
        const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
        ASSERT_EQ(ptrval % alignment, 0UL);
        ptrs[i] = ptr;
      }
      for (size_t i = 0; i < 256; i++) {
        heap->free(ptrs[i]);
      }
    }
  }
  heap->releaseAll();
  mesh::runtime<PageSize>().heap().flushAllBins();
  memset(ptrs, 0, 256 * sizeof(void *));
  free(ptrs);
}

TEST(Alignment, NaturalAlignment) {
  if (getPageSize() == 4096) {
    TestNaturalAlignment<4096>();
  } else {
    TestNaturalAlignment<16384>();
  }
}

template <size_t PageSize>
void TestNonOverlapping() {
  auto heap = ThreadLocalHeap<PageSize>::GetHeap();

  const auto a = heap->malloc(-8);
  const auto b = heap->malloc(-8);

  // we should return nullptr for crazy allocations like this.
  // Fixes #62
  ASSERT_EQ(a, nullptr);
  ASSERT_EQ(b, nullptr);
}

TEST(Alignment, NonOverlapping) {
  if (getPageSize() == 4096) {
    TestNonOverlapping<4096>();
  } else {
    TestNonOverlapping<16384>();
  }
}
