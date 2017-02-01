// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_INTERNAL_H
#define MESH_INTERNAL_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void debug(const char* fmt, ...);

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

namespace internal {
using namespace HL;

// assertions that don't attempt to recursively malloc
void __attribute__((noreturn))
__mesh_assert_fail(const char* assertion, const char* file, int line, const char* fmt, ...);

// for mesh-internal data structures, like heap metadata
class Heap : public ExactlyOneHeap<LockedHeap<PosixLockType, BumpAlloc<16384 * 8, MmapHeap, 16>>> {};

template <typename T>
struct StlAllocator {
  typedef T value_type;
  T* allocate(std::size_t n) {
    return reinterpret_cast<T*>(Heap().malloc(n));
  }
  void deallocate(T* p, std::size_t n) {
    Heap().free(p);
  }
};

template <class T, class U>
bool operator==(const StlAllocator<T>&, const StlAllocator<U>&);
template <class T, class U>
bool operator!=(const StlAllocator<T>&, const StlAllocator<U>&);

template <typename K, typename V>
using unordered_map = std::unordered_map<K, V, hash<K>, equal_to<K>, StlAllocator<pair<const K, V>>>;

template <typename T>
using vector = std::vector<T, StlAllocator<T>>;

}
}

#endif  // MESH_INTERNAL_H
