// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Benchmark comparing different approaches for computing object index from byte offset
// This is a hot-path operation in free() and other MiniHeap operations.
//
// Approaches:
// 1. Float reciprocal: index = (size_t)(offset * (1.0f / object_size))
// 2. Integer magic division (32-bit): index = (offset * magic32) >> shift
// 3. Integer magic division (64-bit): index = (offset * magic64) >> 48
// 4. Direct integer division (baseline): index = offset / object_size

#include <cstdint>
#include <cstddef>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

#include "size_class_reciprocals.h"

#ifndef ATTRIBUTE_ALWAYS_INLINE
#define ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#endif

#ifndef ATTRIBUTE_NEVER_INLINE
#define ATTRIBUTE_NEVER_INLINE __attribute__((noinline))
#endif

using namespace mesh;

// Magic division implementations for benchmarking only
// These are not used in production code - only float reciprocal is used.
namespace magic_div {

struct DivMagic {
  uint32_t magic;   // multiplier
  uint8_t shift;    // right shift amount after multiply
  uint8_t add_one;  // 1 if we need to add 1 after shift (for "round-up" cases)
  uint8_t pad[2];   // padding to 8 bytes
};

constexpr DivMagic computeMagic(uint32_t divisor) {
  if (divisor == 0) {
    return {0, 0, 0, {0, 0}};
  }

  // For power-of-2 divisors, use exact shift
  if ((divisor & (divisor - 1)) == 0) {
    uint8_t shift = 0;
    uint32_t d = divisor;
    while (d > 1) {
      d >>= 1;
      shift++;
    }
    return {1, shift, 0, {0, 0}};
  }

  // For non-power-of-2, use magic division
  uint64_t m64 = ((1ULL << 32) + divisor - 1) / divisor;

  if (m64 <= UINT32_MAX) {
    uint32_t magic = static_cast<uint32_t>(m64);
    uint32_t maxMultiple = (16384 / divisor) * divisor;
    uint32_t expectedIndex = maxMultiple / divisor;
    uint64_t computed = (static_cast<uint64_t>(maxMultiple) * magic) >> 32;

    if (computed == expectedIndex) {
      return {magic, 32, 0, {0, 0}};
    }
  }

  uint32_t magic = static_cast<uint32_t>(((1ULL << 32) + divisor - 1) / divisor);
  return {magic, 32, 1, {0, 0}};
}

inline constexpr DivMagic kMagicTable[kClassSizesMax] = {
    computeMagic(16),   computeMagic(16),   computeMagic(32),   computeMagic(48),   computeMagic(64),
    computeMagic(80),   computeMagic(96),   computeMagic(112),  computeMagic(128),  computeMagic(160),
    computeMagic(192),  computeMagic(224),  computeMagic(256),  computeMagic(320),  computeMagic(384),
    computeMagic(448),  computeMagic(512),  computeMagic(640),  computeMagic(768),  computeMagic(896),
    computeMagic(1024), computeMagic(2048), computeMagic(4096), computeMagic(8192), computeMagic(16384),
};

inline size_t ATTRIBUTE_ALWAYS_INLINE computeIndex(size_t byteOffset, uint32_t sizeClass) {
  const DivMagic &m = kMagicTable[sizeClass];
  uint64_t result = (static_cast<uint64_t>(byteOffset) * m.magic) >> m.shift;
  return static_cast<size_t>(result);
}

}  // namespace magic_div

namespace magic_div64 {

constexpr uint64_t computeMagic64(uint32_t divisor) {
  if (divisor == 0)
    return 0;
  return ((1ULL << 48) + divisor - 1) / divisor;
}

inline constexpr uint64_t kMagicTable64[kClassSizesMax] = {
    computeMagic64(16),   computeMagic64(16),   computeMagic64(32),   computeMagic64(48),   computeMagic64(64),
    computeMagic64(80),   computeMagic64(96),   computeMagic64(112),  computeMagic64(128),  computeMagic64(160),
    computeMagic64(192),  computeMagic64(224),  computeMagic64(256),  computeMagic64(320),  computeMagic64(384),
    computeMagic64(448),  computeMagic64(512),  computeMagic64(640),  computeMagic64(768),  computeMagic64(896),
    computeMagic64(1024), computeMagic64(2048), computeMagic64(4096), computeMagic64(8192), computeMagic64(16384),
};

inline size_t ATTRIBUTE_ALWAYS_INLINE computeIndex(size_t byteOffset, uint32_t sizeClass) {
#if defined(__SIZEOF_INT128__)
  __uint128_t product = static_cast<__uint128_t>(byteOffset) * kMagicTable64[sizeClass];
  return static_cast<size_t>(product >> 48);
#else
  uint64_t magic = kMagicTable64[sizeClass];
  uint64_t a_lo = byteOffset & 0xFFFFFFFF;
  uint64_t a_hi = byteOffset >> 32;
  uint64_t b_lo = magic & 0xFFFFFFFF;
  uint64_t b_hi = magic >> 32;

  uint64_t p0 = a_lo * b_lo;
  uint64_t p1 = a_lo * b_hi;
  uint64_t p2 = a_hi * b_lo;
  uint64_t p3 = a_hi * b_hi;

  uint64_t carry = ((p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF)) >> 32;
  uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + carry;
  uint64_t lo = p0 + (p1 << 32) + (p2 << 32);

  return static_cast<size_t>((hi << 16) | (lo >> 48));
#endif
}

}  // namespace magic_div64

// Test configuration - use explicit values to avoid conflicts with mesh namespace
static constexpr size_t kBenchPageSize4K = 4096;
static constexpr size_t kBenchPageSize16K = 16384;

// Size classes to test (indices into SizeMap::class_to_size_)
// We test a representative sample: small, medium, and large sizes
// Also include both power-of-2 and non-power-of-2 sizes
static constexpr uint32_t kTestSizeClasses[] = {
    0,   // 16 bytes (power of 2, smallest)
    3,   // 48 bytes (non-power-of-2, common small size)
    6,   // 96 bytes (non-power-of-2)
    8,   // 128 bytes (power of 2)
    12,  // 256 bytes (power of 2)
    16,  // 512 bytes (power of 2)
    20,  // 1024 bytes (power of 2, largest "small" size)
};
static constexpr size_t kNumTestClasses = sizeof(kTestSizeClasses) / sizeof(kTestSizeClasses[0]);

// Get object size for a size class - mirror of SizeMap::class_to_size_
static constexpr int32_t kClassToSize[kClassSizesMax] = {
    16,  16,  32,  48,  64,  80,  96,  112,  128,  160,  192,  224,   256,
    320, 384, 448, 512, 640, 768, 896, 1024, 2048, 4096, 8192, 16384,
};

// Pre-generate test offsets for each size class
// These are valid byte offsets (multiples of object size) within a page
struct TestData {
  uint32_t sizeClass;
  uint32_t objectSize;
  uint32_t maxObjects;
  uint32_t offsets[1024];  // enough for 16K page / 16 byte objects
  size_t numOffsets;
};

static TestData generateTestData(uint32_t sizeClass, size_t pageSize) {
  TestData data;
  data.sizeClass = sizeClass;
  data.objectSize = kClassToSize[sizeClass];
  data.maxObjects = pageSize / data.objectSize;
  data.numOffsets = data.maxObjects;

  for (size_t i = 0; i < data.maxObjects && i < 1024; i++) {
    data.offsets[i] = i * data.objectSize;
  }

  return data;
}

// Baseline: Direct integer division
static size_t ATTRIBUTE_NEVER_INLINE computeIndexDirect(size_t byteOffset, uint32_t objectSize) {
  return byteOffset / objectSize;
}

// Verify correctness of all approaches
static void verifyCorrectness() {
  bool hasErrors = false;
  for (size_t pageSize : {kBenchPageSize4K, kBenchPageSize16K}) {
    for (uint32_t sc : kTestSizeClasses) {
      TestData data = generateTestData(sc, pageSize);

      for (size_t i = 0; i < data.numOffsets; i++) {
        uint32_t offset = data.offsets[i];
        size_t expected = offset / data.objectSize;

        size_t floatResult = float_recip::computeIndex(offset, sc);
        size_t magic32Result = magic_div::computeIndex(offset, sc);
        size_t magic64Result = magic_div64::computeIndex(offset, sc);

        if (floatResult != expected) {
          fprintf(stderr, "FLOAT MISMATCH: sc=%u offset=%u expected=%zu got=%zu\n", sc, offset, expected, floatResult);
          hasErrors = true;
        }
        if (magic32Result != expected) {
          fprintf(stderr, "MAGIC32 MISMATCH: sc=%u offset=%u expected=%zu got=%zu\n", sc, offset, expected,
                  magic32Result);
          hasErrors = true;
        }
        if (magic64Result != expected) {
          fprintf(stderr, "MAGIC64 MISMATCH: sc=%u offset=%u expected=%zu got=%zu\n", sc, offset, expected,
                  magic64Result);
          hasErrors = true;
        }
      }
    }
  }
  if (!hasErrors) {
    fprintf(stderr, "All verification checks passed.\n");
  }
}

// Benchmark: Direct integer division (baseline)
static void BM_IndexCompute_DirectDivision(benchmark::State &state) {
  const uint32_t sizeClass = kTestSizeClasses[state.range(0) % kNumTestClasses];
  const size_t pageSize = state.range(1);
  TestData data = generateTestData(sizeClass, pageSize);

  size_t sum = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < data.numOffsets; i++) {
      sum += computeIndexDirect(data.offsets[i], data.objectSize);
    }
  }
  benchmark::DoNotOptimize(sum);

  state.SetItemsProcessed(state.iterations() * data.numOffsets);
  state.SetLabel("objSize=" + std::to_string(data.objectSize));
}

// Benchmark: Float reciprocal (current Mesh approach, moved to table)
static void BM_IndexCompute_FloatReciprocal(benchmark::State &state) {
  const uint32_t sizeClass = kTestSizeClasses[state.range(0) % kNumTestClasses];
  const size_t pageSize = state.range(1);
  TestData data = generateTestData(sizeClass, pageSize);

  size_t sum = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < data.numOffsets; i++) {
      sum += float_recip::computeIndex(data.offsets[i], data.sizeClass);
    }
  }
  benchmark::DoNotOptimize(sum);

  state.SetItemsProcessed(state.iterations() * data.numOffsets);
  state.SetLabel("objSize=" + std::to_string(data.objectSize));
}

// Benchmark: Integer magic division (32-bit magic, variable shift)
static void BM_IndexCompute_MagicDiv32(benchmark::State &state) {
  const uint32_t sizeClass = kTestSizeClasses[state.range(0) % kNumTestClasses];
  const size_t pageSize = state.range(1);
  TestData data = generateTestData(sizeClass, pageSize);

  size_t sum = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < data.numOffsets; i++) {
      sum += magic_div::computeIndex(data.offsets[i], data.sizeClass);
    }
  }
  benchmark::DoNotOptimize(sum);

  state.SetItemsProcessed(state.iterations() * data.numOffsets);
  state.SetLabel("objSize=" + std::to_string(data.objectSize));
}

// Benchmark: Integer magic division (64-bit magic, fixed 48-bit shift)
static void BM_IndexCompute_MagicDiv64(benchmark::State &state) {
  const uint32_t sizeClass = kTestSizeClasses[state.range(0) % kNumTestClasses];
  const size_t pageSize = state.range(1);
  TestData data = generateTestData(sizeClass, pageSize);

  size_t sum = 0;
  for (auto _ : state) {
    for (size_t i = 0; i < data.numOffsets; i++) {
      sum += magic_div64::computeIndex(data.offsets[i], data.sizeClass);
    }
  }
  benchmark::DoNotOptimize(sum);

  state.SetItemsProcessed(state.iterations() * data.numOffsets);
  state.SetLabel("objSize=" + std::to_string(data.objectSize));
}

// Benchmark with mixed size classes (more realistic workload)
static void BM_IndexCompute_Mixed_DirectDivision(benchmark::State &state) {
  const size_t pageSize = state.range(0);
  std::vector<TestData> allData;
  for (uint32_t sc : kTestSizeClasses) {
    allData.push_back(generateTestData(sc, pageSize));
  }

  size_t sum = 0;
  size_t opsPerIter = 0;
  for (const auto &data : allData) {
    opsPerIter += data.numOffsets;
  }

  for (auto _ : state) {
    for (const auto &data : allData) {
      for (size_t i = 0; i < data.numOffsets; i++) {
        sum += computeIndexDirect(data.offsets[i], data.objectSize);
      }
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * opsPerIter);
}

static void BM_IndexCompute_Mixed_FloatReciprocal(benchmark::State &state) {
  const size_t pageSize = state.range(0);
  std::vector<TestData> allData;
  for (uint32_t sc : kTestSizeClasses) {
    allData.push_back(generateTestData(sc, pageSize));
  }

  size_t sum = 0;
  size_t opsPerIter = 0;
  for (const auto &data : allData) {
    opsPerIter += data.numOffsets;
  }

  for (auto _ : state) {
    for (const auto &data : allData) {
      for (size_t i = 0; i < data.numOffsets; i++) {
        sum += float_recip::computeIndex(data.offsets[i], data.sizeClass);
      }
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * opsPerIter);
}

static void BM_IndexCompute_Mixed_MagicDiv32(benchmark::State &state) {
  const size_t pageSize = state.range(0);
  std::vector<TestData> allData;
  for (uint32_t sc : kTestSizeClasses) {
    allData.push_back(generateTestData(sc, pageSize));
  }

  size_t sum = 0;
  size_t opsPerIter = 0;
  for (const auto &data : allData) {
    opsPerIter += data.numOffsets;
  }

  for (auto _ : state) {
    for (const auto &data : allData) {
      for (size_t i = 0; i < data.numOffsets; i++) {
        sum += magic_div::computeIndex(data.offsets[i], data.sizeClass);
      }
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * opsPerIter);
}

static void BM_IndexCompute_Mixed_MagicDiv64(benchmark::State &state) {
  const size_t pageSize = state.range(0);
  std::vector<TestData> allData;
  for (uint32_t sc : kTestSizeClasses) {
    allData.push_back(generateTestData(sc, pageSize));
  }

  size_t sum = 0;
  size_t opsPerIter = 0;
  for (const auto &data : allData) {
    opsPerIter += data.numOffsets;
  }

  for (auto _ : state) {
    for (const auto &data : allData) {
      for (size_t i = 0; i < data.numOffsets; i++) {
        sum += magic_div64::computeIndex(data.offsets[i], data.sizeClass);
      }
    }
  }
  benchmark::DoNotOptimize(sum);
  state.SetItemsProcessed(state.iterations() * opsPerIter);
}

// Register benchmarks with different size classes and page sizes
// Args: (size_class_index, page_size)
BENCHMARK(BM_IndexCompute_DirectDivision)
    ->Args({0, kBenchPageSize4K})
    ->Args({0, kBenchPageSize16K})  // 16 bytes
    ->Args({1, kBenchPageSize4K})
    ->Args({1, kBenchPageSize16K})  // 48 bytes
    ->Args({2, kBenchPageSize4K})
    ->Args({2, kBenchPageSize16K})  // 96 bytes
    ->Args({3, kBenchPageSize4K})
    ->Args({3, kBenchPageSize16K})  // 128 bytes
    ->Args({4, kBenchPageSize4K})
    ->Args({4, kBenchPageSize16K})  // 256 bytes
    ->Args({5, kBenchPageSize4K})
    ->Args({5, kBenchPageSize16K})  // 512 bytes
    ->Args({6, kBenchPageSize4K})
    ->Args({6, kBenchPageSize16K});  // 1024 bytes

BENCHMARK(BM_IndexCompute_FloatReciprocal)
    ->Args({0, kBenchPageSize4K})
    ->Args({0, kBenchPageSize16K})
    ->Args({1, kBenchPageSize4K})
    ->Args({1, kBenchPageSize16K})
    ->Args({2, kBenchPageSize4K})
    ->Args({2, kBenchPageSize16K})
    ->Args({3, kBenchPageSize4K})
    ->Args({3, kBenchPageSize16K})
    ->Args({4, kBenchPageSize4K})
    ->Args({4, kBenchPageSize16K})
    ->Args({5, kBenchPageSize4K})
    ->Args({5, kBenchPageSize16K})
    ->Args({6, kBenchPageSize4K})
    ->Args({6, kBenchPageSize16K});

BENCHMARK(BM_IndexCompute_MagicDiv32)
    ->Args({0, kBenchPageSize4K})
    ->Args({0, kBenchPageSize16K})
    ->Args({1, kBenchPageSize4K})
    ->Args({1, kBenchPageSize16K})
    ->Args({2, kBenchPageSize4K})
    ->Args({2, kBenchPageSize16K})
    ->Args({3, kBenchPageSize4K})
    ->Args({3, kBenchPageSize16K})
    ->Args({4, kBenchPageSize4K})
    ->Args({4, kBenchPageSize16K})
    ->Args({5, kBenchPageSize4K})
    ->Args({5, kBenchPageSize16K})
    ->Args({6, kBenchPageSize4K})
    ->Args({6, kBenchPageSize16K});

BENCHMARK(BM_IndexCompute_MagicDiv64)
    ->Args({0, kBenchPageSize4K})
    ->Args({0, kBenchPageSize16K})
    ->Args({1, kBenchPageSize4K})
    ->Args({1, kBenchPageSize16K})
    ->Args({2, kBenchPageSize4K})
    ->Args({2, kBenchPageSize16K})
    ->Args({3, kBenchPageSize4K})
    ->Args({3, kBenchPageSize16K})
    ->Args({4, kBenchPageSize4K})
    ->Args({4, kBenchPageSize16K})
    ->Args({5, kBenchPageSize4K})
    ->Args({5, kBenchPageSize16K})
    ->Args({6, kBenchPageSize4K})
    ->Args({6, kBenchPageSize16K});

// Mixed workload benchmarks
BENCHMARK(BM_IndexCompute_Mixed_DirectDivision)->Arg(kBenchPageSize4K)->Arg(kBenchPageSize16K);
BENCHMARK(BM_IndexCompute_Mixed_FloatReciprocal)->Arg(kBenchPageSize4K)->Arg(kBenchPageSize16K);
BENCHMARK(BM_IndexCompute_Mixed_MagicDiv32)->Arg(kBenchPageSize4K)->Arg(kBenchPageSize16K);
BENCHMARK(BM_IndexCompute_Mixed_MagicDiv64)->Arg(kBenchPageSize4K)->Arg(kBenchPageSize16K);

// Run verification before benchmarks
static void runVerification() {
  fprintf(stderr, "Verifying correctness of all index computation methods...\n");
  verifyCorrectness();
  fprintf(stderr, "\n");
}

int main(int argc, char **argv) {
  runVerification();

  // Print table sizes for comparison
  fprintf(stderr, "Table sizes:\n");
  fprintf(stderr, "  Float reciprocal: %zu bytes (%zu entries x %zu bytes)\n", sizeof(float_recip::kReciprocals),
          kClassSizesMax, sizeof(float));
  fprintf(stderr, "  Magic div 32-bit: %zu bytes (%zu entries x %zu bytes)\n", sizeof(magic_div::kMagicTable),
          kClassSizesMax, sizeof(magic_div::DivMagic));
  fprintf(stderr, "  Magic div 64-bit: %zu bytes (%zu entries x %zu bytes)\n", sizeof(magic_div64::kMagicTable64),
          kClassSizesMax, sizeof(uint64_t));
  fprintf(stderr, "\n");

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
