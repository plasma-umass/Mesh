// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_MESHING_H
#define MESH_MESHING_H

#include <algorithm>
#include <atomic>
#include <limits>

#include "bitmap.h"
#include "common.h"
#include "internal.h"
#include "mini_heap.h"

namespace mesh {

using internal::Bitmap;

inline bool bitmapsMeshable(const Bitmap::word_t *__restrict__ bitmap1, const Bitmap::word_t *__restrict__ bitmap2,
                            size_t byteLen) noexcept {
  d_assert(reinterpret_cast<uintptr_t>(bitmap1) % 16 == 0);
  d_assert(reinterpret_cast<uintptr_t>(bitmap2) % 16 == 0);
  d_assert(byteLen >= 8);
  d_assert(byteLen % 8 == 0);

  // because we hold the global lock (so no miniheap can transition
  // from 'free' to 'attached'), the only possible data race we have
  // here is our read with a write changing a bit from 1 -> 0.  That
  // makes this race benign - we may have a false positive if an
  // allocation is freed (it may cause 2 bitmaps to look like they
  // overlap when _now_ they don't actually), but it won't cause
  // correctness issues.
  const auto bitmapL = (const uint64_t *)__builtin_assume_aligned(bitmap1, 16);
  const auto bitmapR = (const uint64_t *)__builtin_assume_aligned(bitmap2, 16);

  uint64_t result = 0;

  for (size_t i = 0; i < byteLen / sizeof(size_t); i++) {
    result |= bitmapL[i] & bitmapR[i];
  }

  return result == 0;
}

namespace method {

// split miniheaps into two lists in a random order
void halfSplit(MWC &prng, MiniHeapListEntry *miniheaps, SplitArray &left, size_t &leftSize, SplitArray &right,
               size_t &rightSize) noexcept;

void shiftedSplitting(MWC &prng, MiniHeapListEntry *miniheaps, SplitArray &left, SplitArray &right,
                      const function<bool(std::pair<MiniHeap *, MiniHeap *> &&)> &meshFound) noexcept;
}  // namespace method
}  // namespace mesh

#endif  // MESH_MESHING_H
