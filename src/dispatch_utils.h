// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2024 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifndef MESH__DISPATCH_UTILS_H
#define MESH__DISPATCH_UTILS_H

#include "common.h"
#include "runtime.h"

namespace mesh {

// ===================================================================
// Page Size Dispatch Utilities
// ===================================================================
// These utilities eliminate code duplication by providing a clean way
// to dispatch between 4KB and 16KB page size implementations at runtime.
//
// Instead of writing:
//   if (likely(getPageSize() == kPageSize4K)) {
//     runtime<kPageSize4K>().someMethod();
//   } else {
//     runtime<kPageSize16K>().someMethod();
//   }
//
// You can write:
//   dispatchByPageSize([](auto& rt) { rt.someMethod(); });
// ===================================================================

// Dispatch helper that calls a function with the correct runtime instance
template <typename Func>
inline auto dispatchByPageSize(Func &&func) -> decltype(func(runtime<kPageSize4K>())) {
  if (likely(getPageSize() == kPageSize4K)) {
    return func(runtime<kPageSize4K>());
  } else {
    return func(runtime<kPageSize16K>());
  }
}

// Dispatch helper for functions that take no runtime but need page size selection
template <typename Func4K, typename Func16K>
inline auto dispatchByPageSizeEx(Func4K &&func4k, Func16K &&func16k) -> decltype(func4k()) {
  if (likely(getPageSize() == kPageSize4K)) {
    return func4k();
  } else {
    return func16k();
  }
}

// Macro for simpler dispatch when you just need to call a templated function
// Usage: DISPATCH_BY_PAGE_SIZE(someFunction)();
#define DISPATCH_BY_PAGE_SIZE(func) \
  ((likely(getPageSize() == kPageSize4K)) ? (func<kPageSize4K>) : (func<kPageSize16K>))

// Macro for getting the correct runtime instance
// Usage: auto& rt = GET_RUNTIME();
#define GET_RUNTIME() ((likely(getPageSize() == kPageSize4K)) ? runtime<kPageSize4K>() : runtime<kPageSize16K>())

// Macro for getting the correct heap instance
// Usage: auto& heap = GET_HEAP();
#define GET_HEAP() \
  ((likely(getPageSize() == kPageSize4K)) ? runtime<kPageSize4K>().heap() : runtime<kPageSize16K>().heap())

// Template helper to get ThreadLocalHeap for the current page size
template <typename Func>
inline auto withThreadLocalHeap(Func &&func) {
  if (likely(getPageSize() == kPageSize4K)) {
    auto *heap = ThreadLocalHeap<kPageSize4K>::GetHeapIfPresent();
    return func(heap, std::integral_constant<size_t, kPageSize4K>{});
  } else {
    auto *heap = ThreadLocalHeap<kPageSize16K>::GetHeapIfPresent();
    return func(heap, std::integral_constant<size_t, kPageSize16K>{});
  }
}

// Helper for casting MiniHeap pointers based on page size
template <typename Func>
inline auto withMiniHeap(void *mh_void, Func &&func) {
  if (likely(getPageSize() == kPageSize4K)) {
    return func(reinterpret_cast<MiniHeap<kPageSize4K> *>(mh_void));
  } else {
    return func(reinterpret_cast<MiniHeap<kPageSize16K> *>(mh_void));
  }
}

}  // namespace mesh

#endif  // MESH__DISPATCH_UTILS_H