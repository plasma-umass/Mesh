// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_COMMON_H
#define MESH_COMMON_H

#include <cstddef>
#include <cstdint>
#include <ctime>

#include <fcntl.h>

#if !defined(_WIN32)
#ifdef __APPLE__
#define _DARWIN_C_SOURCE  // exposes MAP_ANONYMOUS and MAP_NORESERVE
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

// #include "config.h"

#include "static/log.h"

// from Heap Layers
#include "utility/ilog2.h"

#ifdef __linux__
#define MESH_THROW throw()
#else
#define MESH_THROW
#endif

// MESH_HAVE_TLS is defined to 1 when __thread should be supported.
// We assume __thread is supported on Linux when compiled with Clang or compiled
// against libstdc++ with _GLIBCXX_HAVE_TLS defined. (this check is from Abseil)
#ifdef MESH_HAVE_TLS
#error MESH_HAVE_TLS cannot be directly set
#elif defined(__linux__) && (defined(__clang__) || defined(_GLIBCXX_HAVE_TLS))
#define MESH_HAVE_TLS 1
#endif

namespace mesh {

static constexpr bool kMeshingEnabled = MESHING_ENABLED == 1;

#if defined(_WIN32)
// FIXME(EDB)
static constexpr int kMapShared = 1;
#else
static constexpr int kMapShared = kMeshingEnabled ? MAP_SHARED : MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
#endif

static constexpr size_t kMinObjectSize = 16;
static constexpr size_t kMaxSize = 16384;
static constexpr size_t kClassSizesMax = 25;
static constexpr size_t kAlignment = 8;
static constexpr int kMinAlign = 16;
static constexpr uint64_t kPageSize = 4096;
static constexpr size_t kMaxFastLargeSize = 256 * 1024;  // 256Kb

static constexpr size_t kMaxSplitListSize = 16384;
static constexpr size_t kMaxMergeSets = 4096;

// cutoff to be considered for meshing
static constexpr double kOccupancyCutoff = .8;

// if we have, e.g. a kernel-imposed max_map_count of 2^16 (65k) we
// can only safely have about 30k meshes before we are at risk of
// hitting the max_map_count limit.
static constexpr double kMeshesPerMap = .457;
static constexpr size_t kDefaultMaxMeshCount = 30000;
static constexpr size_t kMaxMeshesPerIteration = 2500;

// maximum number of dirty pages to hold onto before we flush them
// back to the OS (via MeshableArena::scavenge()
static constexpr size_t kMaxDirtyPageThreshold = 1 << 14;  // 64 MB in pages
static constexpr size_t kMinDirtyPageThreshold = 32;       // 128 KB in pages

static constexpr uint32_t kSpanClassCount = 256;

static constexpr int kNumBins = 25;  // 16Kb max object size
static constexpr int kDefaultMeshPeriod = 10000;

static constexpr size_t kMinArenaExpansion = 4096;  // 16 MB in pages

// ensures we amortize the cost of going to the global heap enough
static constexpr uint64_t kMinStringLen = 8;
static constexpr size_t kMiniheapRefillGoalSize = 4 * 1024;
static constexpr size_t kMaxMiniheapsPerShuffleVector = 24;

// shuffle vector features
static constexpr int16_t kMaxShuffleVectorLength = 256;  // sizeof(uint8_t) << 8
static constexpr bool kEnableShuffleOnInit = SHUFFLE_ON_INIT == 1;
static constexpr bool kEnableShuffleOnFree = SHUFFLE_ON_FREE == 1;

// madvise(DONTDUMP) the heap to make reasonable coredumps
static constexpr bool kAdviseDump = false;

static constexpr std::chrono::milliseconds kZeroMs{0};
static constexpr std::chrono::milliseconds kMeshPeriodMs{100};  // 100 ms

// controls aspects of miniheaps
static constexpr size_t kMaxMeshes = 256;  // 1 per bit
#ifdef __APPLE__
static constexpr size_t kArenaSize = 32ULL * 1024ULL * 1024ULL * 1024ULL;  // 16 GB
#else
static constexpr size_t kArenaSize = 64ULL * 1024ULL * 1024ULL * 1024ULL;  // 64 GB
#endif
static constexpr size_t kAltStackSize = 16 * 1024UL;  // 16k sigaltstacks
#define SIGQUIESCE (SIGRTMIN + 7)
#define SIGDUMP (SIGRTMIN + 8)

// BinnedTracker
static constexpr size_t kBinnedTrackerBinCount = 1;
static constexpr size_t kBinnedTrackerMaxEmpty = 128;

static inline constexpr size_t PageCount(size_t sz) {
  return (sz + (kPageSize - 1)) / kPageSize;
}

static inline constexpr size_t RoundUpToPage(size_t sz) {
  return kPageSize * PageCount(sz);
}

namespace powerOfTwo {
static constexpr size_t kMinObjectSize = 8;

inline constexpr size_t ByteSizeForClass(const int i) {
  return static_cast<size_t>(1ULL << (i + staticlog(kMinObjectSize)));
}

inline constexpr int ClassForByteSize(const size_t sz) {
  return static_cast<int>(HL::ilog2((sz < 8) ? 8 : sz) - staticlog(kMinObjectSize));
}
}  // namespace powerOfTwo

}  // namespace mesh

using std::condition_variable;
using std::function;
using std::lock_guard;
using std::mt19937_64;
using std::mutex;
// using std::shared_lock;
// using std::shared_mutex;
using std::unique_lock;

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define ATTRIBUTE_UNUSED __attribute__((unused))
#define ATTRIBUTE_NEVER_INLINE __attribute__((noinline))
#define ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#define ATTRIBUTE_NORETURN __attribute__((noreturn))
#define ATTRIBUTE_ALIGNED(s) __attribute__((aligned(s)))
#define CACHELINE_SIZE 64
#define CACHELINE_ALIGNED ATTRIBUTE_ALIGNED(CACHELINE_SIZE)
#define CACHELINE_ALIGNED_FN CACHELINE_ALIGNED
#define PAGE_ALIGNED ATTRIBUTE_ALIGNED(kPageSize)

#define MESH_EXPORT __attribute__((visibility("default")))

#define ATTR_INITIAL_EXEC __attribute__((tls_model("initial-exec")))

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

// runtime debug-level asserts
#ifndef NDEBUG
#define d_assert_msg(expr, fmt, ...) \
  ((likely(expr))                    \
       ? static_cast<void>(0)        \
       : mesh::internal::__mesh_assert_fail(#expr, __FILE__, __PRETTY_FUNCTION__, __LINE__, fmt, __VA_ARGS__))

#define d_assert(expr)                   \
  ((likely(expr)) ? static_cast<void>(0) \
                  : mesh::internal::__mesh_assert_fail(#expr, __FILE__, __PRETTY_FUNCTION__, __LINE__, ""))
#else
#define d_assert_msg(expr, fmt, ...)
#define d_assert(expr)
#endif

// like d_assert, but still executed in release builds
#define hard_assert_msg(expr, fmt, ...) \
  ((likely(expr))                       \
       ? static_cast<void>(0)           \
       : mesh::internal::__mesh_assert_fail(#expr, __FILE__, __PRETTY_FUNCTION__, __LINE__, fmt, __VA_ARGS__))
#define hard_assert(expr)                \
  ((likely(expr)) ? static_cast<void>(0) \
                  : mesh::internal::__mesh_assert_fail(#expr, __FILE__, __PRETTY_FUNCTION__, __LINE__, ""))

namespace mesh {

// logging
void debug(const char *fmt, ...);

namespace internal {
// assertions that don't attempt to recursively malloc
void __attribute__((noreturn))
__mesh_assert_fail(const char *assertion, const char *file, const char *func, int line, const char *fmt, ...);

inline static mutex *getSeedMutex() {
  static char muBuf[sizeof(mutex)];
  static mutex *mu = new (muBuf) mutex();
  return mu;
}

// we must re-initialize our seed on program startup and after fork.
// Must be called with getSeedMutex() held
inline mt19937_64 *initSeed() {
  static char mtBuf[sizeof(mt19937_64)];

  static_assert(sizeof(mt19937_64::result_type) == sizeof(uint64_t), "expected 64-bit result_type for PRNG");

  // seed this Mersenne Twister PRNG with entropy from the host OS
  int fd = open("/dev/urandom", O_RDONLY);
  unsigned long buf;
  auto sz = read(fd, (void *)&buf, sizeof(unsigned long));
  hard_assert(sz == sizeof(unsigned long));
  //  std::random_device rd;
  // return new (mtBuf) std::mt19937_64(rd());
  return new (mtBuf) std::mt19937_64(buf);
}

// cryptographically-strong thread-safe PRNG seed
inline uint64_t seed() {
  static mt19937_64 *mt = NULL;

  lock_guard<mutex> lock(*getSeedMutex());

  if (unlikely(mt == nullptr))
    mt = initSeed();

  return (*mt)();
}
}  // namespace internal

namespace time {
using clock = std::chrono::high_resolution_clock;
using time_point = std::chrono::time_point<clock>;

inline time_point ATTRIBUTE_ALWAYS_INLINE now() {
#ifdef __linux__
  using namespace std::chrono;
  struct timespec tp;
  auto err = clock_gettime(CLOCK_MONOTONIC_COARSE, &tp);
  hard_assert(err == 0);
  return time_point(seconds(tp.tv_sec) + nanoseconds(tp.tv_nsec));
#else
  return std::chrono::high_resolution_clock::now();
#endif
}
}  // namespace time

#define PREDICT_TRUE likely

// from tcmalloc/gperftools
class SizeMap {
private:
  //-------------------------------------------------------------------
  // Mapping from size to size_class and vice versa
  //-------------------------------------------------------------------

  // Sizes <= 1024 have an alignment >= 8.  So for such sizes we have an
  // array indexed by ceil(size/8).  Sizes > 1024 have an alignment >= 128.
  // So for these larger sizes we have an array indexed by ceil(size/128).
  //
  // We flatten both logical arrays into one physical array and use
  // arithmetic to compute an appropriate index.  The constants used by
  // ClassIndex() were selected to make the flattening work.
  //
  // Examples:
  //   Size       Expression                      Index
  //   -------------------------------------------------------
  //   0          (0 + 7) / 8                     0
  //   1          (1 + 7) / 8                     1
  //   ...
  //   1024       (1024 + 7) / 8                  128
  //   1025       (1025 + 127 + (120<<7)) / 128   129
  //   ...
  //   32768      (32768 + 127 + (120<<7)) / 128  376
  static const int kMaxSmallSize = 1024;
  static const size_t kClassArraySize = ((kMaxSize + 127 + (120 << 7)) >> 7) + 1;
  static const unsigned char class_array_[kClassArraySize];

  static inline size_t SmallSizeClass(size_t s) {
    return (static_cast<uint32_t>(s) + 7) >> 3;
  }

  static inline size_t LargeSizeClass(size_t s) {
    return (static_cast<uint32_t>(s) + 127 + (120 << 7)) >> 7;
  }

  // If size is no more than kMaxSize, compute index of the
  // class_array[] entry for it, putting the class index in output
  // parameter idx and returning true. Otherwise return false.
  static inline bool ATTRIBUTE_ALWAYS_INLINE ClassIndexMaybe(size_t s, uint32_t *idx) {
    if (PREDICT_TRUE(s <= kMaxSmallSize)) {
      *idx = (static_cast<uint32_t>(s) + 7) >> 3;
      return true;
    } else if (s <= kMaxSize) {
      *idx = (static_cast<uint32_t>(s) + 127 + (120 << 7)) >> 7;
      return true;
    }
    return false;
  }

  // Compute index of the class_array[] entry for a given size
  static inline size_t ClassIndex(size_t s) {
    // Use unsigned arithmetic to avoid unnecessary sign extensions.
    d_assert(s <= kMaxSize);
    if (PREDICT_TRUE(s <= kMaxSmallSize)) {
      return SmallSizeClass(s);
    } else {
      return LargeSizeClass(s);
    }
  }

  // Mapping from size class to max size storable in that class
  static const int32_t class_to_size_[kClassSizesMax];

public:
  static constexpr size_t num_size_classes = 25;

  // Constructor should do nothing since we rely on explicit Init()
  // call, which may or may not be called before the constructor runs.
  SizeMap() {
  }

  static inline int SizeClass(size_t size) {
    return class_array_[ClassIndex(size)];
  }

  // Check if size is small enough to be representable by a size
  // class, and if it is, put matching size class into *cl. Returns
  // true iff matching size class was found.
  static inline bool ATTRIBUTE_ALWAYS_INLINE GetSizeClass(size_t size, uint32_t *cl) {
    uint32_t idx;
    if (!ClassIndexMaybe(size, &idx)) {
      return false;
    }
    *cl = class_array_[idx];
    return true;
  }

  // Get the byte-size for a specified class
  // static inline int32_t ATTRIBUTE_ALWAYS_INLINE ByteSizeForClass(uint32_t cl) {
  static inline size_t ATTRIBUTE_ALWAYS_INLINE ByteSizeForClass(int32_t cl) {
    return class_to_size_[static_cast<uint32_t>(cl)];
  }

  // Mapping from size class to max size storable in that class
  static inline int32_t class_to_size(uint32_t cl) {
    return class_to_size_[cl];
  }
};
}  // namespace mesh

#endif  // MESH_COMMON_H
