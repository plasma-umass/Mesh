// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2020 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <atomic>
#include <random>

#include <benchmark/benchmark.h>

#include "internal.h"
#include "global_heap.h"
#include "runtime.h"
#include "shuffle_vector.h"

using namespace mesh;

static constexpr size_t kMiniHeapCount = 2 << 14;
static constexpr uint32_t kObjSize = 16;
static constexpr uint32_t kObjCount = 256;

template <size_t PageSize>
static void ATTRIBUTE_NEVER_INLINE initializeMiniHeaps(FixedArray<MiniHeap<PageSize>, kMiniHeapCount> &miniheaps) {
  // https://xkcd.com/221/ ; ensure results are repeatable (each benchmark sees the same bits set)
  static constexpr std::mt19937::result_type kSeed{3852235742};
  auto rng = std::bind(std::uniform_int_distribution<uint64_t>{}, std::mt19937{kSeed});

  for (size_t i = 0; i < kMiniHeapCount; i++) {
    MiniHeap<PageSize> *mh = miniheaps[i];

    // this is way way faster than setting each individual bit
    auto bits = mh->writableBitmap().mut_bits();
    for (size_t j = 0; j < 4; j++) {
      bits[j] = rng();
    }
  }
}

template <size_t PageSize>
static void ATTRIBUTE_NEVER_INLINE initAndRefill(FixedArray<MiniHeap<PageSize>, kMiniHeapCount> &array, size_t &n,
                                                 ShuffleVector<PageSize> &sv) {
  {
    FixedArray<MiniHeap<PageSize>, kMaxMiniheapsPerShuffleVector> &miniheaps = sv.miniheaps();
    miniheaps.clear();
    for (size_t i = 0; i < kMaxMiniheapsPerShuffleVector; i++) {
      if (unlikely(n >= kMiniHeapCount)) {
        n = 0;
      }
      auto mh = array[n++];
      // Reset bitmap to ensure we can allocate from it again
      // We just clear it to 0 (empty) to ensure progress
      mh->takeBitmap();

      miniheaps.append(mh);
    }
  }
  sv.reinit();

  bool cont = true;
  // it may take a few iterations to pull the available offsets from our miniheaps
  while (likely(cont)) {
    while (likely(!sv.isExhausted())) {
      char *ptr = reinterpret_cast<char *>(sv.malloc());
      hard_assert(ptr != nullptr);
      ptr[0] = 'x';
    }

    cont = sv.localRefill();
  }
}

template <size_t PageSize>
static void BM_LocalRefill1Impl(benchmark::State &state) {
  mesh::debug("local refill test!\n");
  const auto tid = gettid();
  GlobalHeap<PageSize> &gheap = runtime<PageSize>().heap();

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  static FixedArray<MiniHeap<PageSize>, kMiniHeapCount> array{};
  if (array.size() == 0) {
    // const size_t initialAllocCount = gheap.getAllocatedMiniheapCount();
    // hard_assert_msg(initialAllocCount == 0UL, "expected 0 initial MHs, not %zu", initialAllocCount);

    const uint32_t objCount = PageSize / kObjSize;

    gheap.lock();
    while (!array.full()) {
      // SizeClass(kObjSize), 1 page, objCount, kObjSize
      auto mh = gheap.allocMiniheapLocked(SizeMap::SizeClass(kObjSize), 1, objCount, kObjSize);
      // We must attach it to prevent it from being considered "free" immediately if we were to return it
      // But here we just hold it in array.
      // The original allocSmallMiniheaps did setAttached.
      mh->setAttached(tid, gheap.freelistFor(mh->freelistId(), mh->sizeClass()));
      array.append(mh);
    }
    gheap.unlock();

    mesh::debug("initializing the miniheaps\n");
  }

  // always reinitialize the bitmaps (will be same pattern each time)
  initializeMiniHeaps<PageSize>(array);

  ShuffleVector<PageSize> sv{};
  sv.initialInit(gheap.arenaBegin(), kObjSize);

  size_t n = 0;

  for (auto _ : state) {
    initAndRefill<PageSize>(array, n, sv);
  }

  // for (size_t i = 0; i < kMiniHeapCount; i++) {
  //   MiniHeap *mh = array[i];
  //   gheap.freeMiniheap(mh);
  // }
  // array.clear();

  sv.miniheaps().clear();
}

static void BM_LocalRefill1(benchmark::State &state) {
  // Ensure runtime is initialized
  const size_t pageSize = getPageSize();
  if (pageSize == 4096) {
    runtime<4096>().createSignalFd();
    BM_LocalRefill1Impl<4096>(state);
  } else {
    runtime<16384>().createSignalFd();
    BM_LocalRefill1Impl<16384>(state);
  }
}
BENCHMARK(BM_LocalRefill1);

BENCHMARK_MAIN();
