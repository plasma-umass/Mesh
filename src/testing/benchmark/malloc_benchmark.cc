// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

#include <benchmark/benchmark.h>
#include <stdlib.h>
#include <vector>

extern "C" {
void *mesh_malloc(size_t sz);
void mesh_free(void *ptr);
}

static void BM_MallocFree(benchmark::State &state) {
  const size_t size = state.range(0);
  for (auto _ : state) {
    void *ptr = mesh_malloc(size);
    benchmark::DoNotOptimize(ptr);
    mesh_free(ptr);
  }
}

// Register the benchmark with different sizes
BENCHMARK(BM_MallocFree)->Range(16, 4096);
