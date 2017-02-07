// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_INTERNAL_H
#define MESH_INTERNAL_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

using std::lock_guard;
using std::mutex;
using std::mt19937_64;

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void debug(const char *fmt, ...);

#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName &);              \
  void operator=(const TypeName &)

// dynamic (runtime) assert
#ifndef NDEBUG
#define d_assert_msg(expr, fmt, ...)                                                   \
  ({                                                                                   \
    if (likely(expr)) {                                                                \
    } else {                                                                           \
      mesh::internal::__mesh_assert_fail(#expr, __FILE__, __LINE__, fmt, __VA_ARGS__); \
    }                                                                                  \
  })

#define d_assert(expr)                                                                 \
  ({                                                                                   \
    if (likely(expr)) {                                                                \
    } else {                                                                           \
      mesh::internal::__mesh_assert_fail(#expr, __FILE__, __LINE__, "");               \
    }                                                                                  \
  })
#else
#error "don't disable assertions"
#define d_assert(expr, fmt, ...)
#endif

#include "heaplayers.h"

namespace mesh {

inline constexpr size_t class2Size(const int i) {
  auto sz = (size_t)(1ULL << (i + 4));
  return sz;
}

inline int size2Class(const size_t sz) {
  auto cl = (int)HL::ilog2((sz < 8) ? 8 : sz) - 4;
  return cl;
}

namespace internal {

inline static mutex *getSeedMutex(void) {
  static char muBuf[sizeof(mutex)];
  static mutex *mu = new (muBuf) mutex();
  return mu;
}

// we must re-initialize our seed on program startup and after fork.
// Must be called with getSeedMutex() held
mt19937_64 *initSeed() {
  static char mtBuf[sizeof(mt19937_64)];

  static_assert(sizeof(mt19937_64::result_type) == sizeof(uint64_t), "expected 64-bit result_type for PRNG");

  // seed this Mersenne Twister PRNG with entropy from the host OS
  std::random_device rd;
  return new (mtBuf) std::mt19937_64(rd());
}

inline uint64_t seed() {
  static mt19937_64 *mt = NULL;

  lock_guard<mutex> lock(*getSeedMutex());

  if (unlikely(mt == nullptr))
    mt = initSeed();

  return (*mt)();
}

// assertions that don't attempt to recursively malloc
void __attribute__((noreturn))
__mesh_assert_fail(const char *assertion, const char *file, int line, const char *fmt, ...);

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<LockedHeap<PosixLockType, DebugHeap<SizeHeap<BumpAlloc<16384 * 8, MmapHeap, 16>>>>> {
};

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V, hash<K>, equal_to<K>, STLAllocator<pair<const K, V>, Heap>>;

template <typename K, typename V>
using map = std::map<K, V, std::less<K>, STLAllocator<pair<const K, V>, Heap>>;

template <typename T>
using vector = std::vector<T, STLAllocator<T, Heap>>;

// https://stackoverflow.com/questions/529831/returning-the-greatest-key-strictly-less-than-the-given-key-in-a-c-map
template <typename Map>
typename Map::const_iterator greatest_less(Map const &m, typename Map::key_type const &k) {
  typename Map::const_iterator it = m.lower_bound(k);
  if (it != m.begin()) {
    return --it;
  }
  return m.end();
}

// https://stackoverflow.com/questions/529831/returning-the-greatest-key-strictly-less-than-the-given-key-in-a-c-map
template <typename Map>
typename Map::iterator greatest_less(Map &m, typename Map::key_type const &k) {
  typename Map::iterator it = m.lower_bound(k);
  if (it != m.begin()) {
    return --it;
  }
  return m.end();
}
}
}

#endif  // MESH_INTERNAL_H
