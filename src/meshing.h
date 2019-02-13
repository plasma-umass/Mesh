// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MESHING_H
#define MESH__MESHING_H

#include <algorithm>
#include <atomic>
#include <limits>

#include "common.h"

#include "internal.h"
#include "mini_heap.h"

#include "binned_tracker.h"
#include "bitmap.h"

namespace mesh {

using internal::Bitmap;

inline bool bitmapsMeshable(const Bitmap::word_t *__restrict__ bitmap1, const Bitmap::word_t *__restrict__ bitmap2,
                            size_t byteLen) noexcept {
  d_assert(reinterpret_cast<uintptr_t>(bitmap1) % 16 == 0);
  d_assert(reinterpret_cast<uintptr_t>(bitmap2) % 16 == 0);
  d_assert(byteLen >= 8);
  d_assert(byteLen % 8 == 0);

  bitmap1 = (const Bitmap::word_t *)__builtin_assume_aligned(bitmap1, 16);
  bitmap2 = (const Bitmap::word_t *)__builtin_assume_aligned(bitmap2, 16);

  for (size_t i = 0; i < byteLen / sizeof(size_t); i++) {
    if ((bitmap1[i] & bitmap2[i]) != 0) {
      // debug("%zu/%zu bitmap cmp failed: %zx & %zx != 0 (%zx)", i, byteLen, bitmap1[i].load(), bitmap2[i].load(),
      //       bitmap1[i].load() & bitmap2[i].load());
      return false;
    }
  }
  return true;
}

namespace method {

// split miniheaps into two lists in a random order
inline void halfSplit(MWC &prng, BinnedTracker &miniheaps, internal::vector<MiniHeap *> &left,
                      internal::vector<MiniHeap *> &right) noexcept {
  internal::vector<MiniHeap *> bucket = miniheaps.meshingCandidates(kOccupancyCutoff);

  internal::mwcShuffle(bucket.begin(), bucket.end(), prng);

  for (size_t i = 0; i < bucket.size(); i++) {
    auto mh = bucket[i];
    if (!mh->isMeshingCandidate() || mh->fullness() >= kOccupancyCutoff)
      continue;

    if (left.size() <= right.size())
      left.push_back(mh);
    else
      right.push_back(mh);
  }
}

template <size_t t = 64>
inline void shiftedSplitting(MWC &prng, BinnedTracker &miniheaps,
                             const function<void(std::pair<MiniHeap *, MiniHeap *> &&)> &meshFound) noexcept {
  if (miniheaps.partialSize() == 0)
    return;

  internal::vector<MiniHeap *> leftBucket{};   // mutable copy
  internal::vector<MiniHeap *> rightBucket{};  // mutable copy

  halfSplit(prng, miniheaps, leftBucket, rightBucket);

  const auto leftSize = leftBucket.size();
  const auto rightSize = rightBucket.size();

  if (leftSize == 0 || rightSize == 0)
    return;

  const size_t limit = rightSize < t ? rightSize : t;
  constexpr size_t nBytes = 32;
  d_assert(nBytes == leftBucket[0]->bitmap().byteCount());

  size_t foundCount = 0;
  for (size_t j = 0; j < leftSize; j++) {
    const size_t idxLeft = j;
    size_t idxRight = j;
    for (size_t i = 0; i < limit; i++, idxRight++) {
      if (unlikely(idxRight >= rightSize)) {
        idxRight %= rightSize;
      }
      auto h1 = leftBucket[idxLeft];
      auto h2 = rightBucket[idxRight];

      if (h1 == nullptr || h2 == nullptr)
        continue;

      const auto bitmap1 = h1->bitmap().bits();
      const auto bitmap2 = h2->bitmap().bits();

      if (unlikely(mesh::bitmapsMeshable(bitmap1, bitmap2, nBytes))) {
        std::pair<MiniHeap *, MiniHeap *> heaps{h1, h2};
        meshFound(std::move(heaps));
        leftBucket[idxLeft] = nullptr;
        rightBucket[idxRight] = nullptr;
        foundCount++;
        if (foundCount > kMaxMeshesPerIteration) {
          return;
        }
      }
    }
  }
}
}  // namespace method
}  // namespace mesh

#endif  // MESH__MESHING_H
