// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2020 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <atomic>

#include <benchmark/benchmark.h>

#include "internal.h"
#include "global_heap.h"
#include "runtime.h"

using namespace mesh;

static constexpr uint32_t StrLen = 128;
static constexpr uint32_t ObjCount = 32;

static void BM_LocalRefill1(benchmark::State& state) {
  mesh::debug("local refill test!\n");
  const auto tid = gettid();
  GlobalHeap &gheap = runtime().heap();

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  const size_t initialAllocCount = gheap.getAllocatedMiniheapCount();
  std::atomic_thread_fence(std::memory_order_seq_cst);
  hard_assert_msg(initialAllocCount == 0UL, "expected 0 initial MHs, not %zu", initialAllocCount);

  FixedArray<MiniHeap, 1> array{};

  // allocate two miniheaps for the same object size from our global heap
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh1 = array[0];
  array.clear();

  gheap.freeMiniheap(mh1);

  std::string x = "hello";
  for (auto _ : state) {
    std::string copy(x);
  }
}
BENCHMARK(BM_LocalRefill1);

BENCHMARK_MAIN();
