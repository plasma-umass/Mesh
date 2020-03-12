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

static constexpr size_t kMiniHeapCount = 2 << 18;
static constexpr uint32_t kObjSize = 16;
static constexpr uint32_t kObjCount = 256;

static void ATTRIBUTE_NEVER_INLINE initializeMiniHeaps(FixedArray<MiniHeap, kMiniHeapCount> &miniheaps) {
  // https://xkcd.com/221/ ; ensure results are repeatable (each benchmark sees the same bits set)
  static constexpr std::mt19937::result_type kSeed{3852235742};
  auto rng = std::bind(std::uniform_int_distribution<uint64_t>{}, std::mt19937{kSeed});

  for (size_t i = 0; i < kMiniHeapCount; i++) {
    MiniHeap *mh = miniheaps[i];

    // this is way way faster than setting each individual bit
    auto bits = mh->writableBitmap().mut_bits();
    for (size_t j = 0; j < 4; j++) {
      bits[j] = rng();
    }
  }
}

static void ATTRIBUTE_NEVER_INLINE initAndRefill(FixedArray<MiniHeap, kMiniHeapCount> &array, size_t &n,
                                                 ShuffleVector &sv) {
  {
    FixedArray<MiniHeap, kMaxMiniheapsPerShuffleVector> &miniheaps = sv.miniheaps();
    miniheaps.clear();
    for (size_t i = 0; i < kMaxMiniheapsPerShuffleVector; i++) {
      miniheaps.append(array[n++]);
      hard_assert(n < kMiniHeapCount);
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

static void BM_LocalRefill1(benchmark::State &state) {
  mesh::debug("local refill test!\n");
  const auto tid = gettid();
  GlobalHeap &gheap = runtime().heap();

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  static FixedArray<MiniHeap, kMiniHeapCount> array{};
  if (array.size() == 0) {
    const size_t initialAllocCount = gheap.getAllocatedMiniheapCount();
    hard_assert_msg(initialAllocCount == 0UL, "expected 0 initial MHs, not %zu", initialAllocCount);

    // allocate two miniheaps for the same object size from our global heap
    gheap.allocSmallMiniheaps(SizeMap::SizeClass(kObjSize), kObjSize, array, tid);
    mesh::debug("initializing the miniheaps\n");
  }

  // always reinitialize the bitmaps (will be same pattern each time)
  initializeMiniHeaps(array);

  ShuffleVector sv{};
  sv.initialInit(gheap.arenaBegin(), kObjSize);

  size_t n = 0;

  for (auto _ : state) {
    initAndRefill(array, n, sv);
  }

  // for (size_t i = 0; i < kMiniHeapCount; i++) {
  //   MiniHeap *mh = array[i];
  //   gheap.freeMiniheap(mh);
  // }
  // array.clear();

  sv.miniheaps().clear();
}
BENCHMARK(BM_LocalRefill1);

BENCHMARK_MAIN();
