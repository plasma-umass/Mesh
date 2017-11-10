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
#include "miniheap.h"

#include "binnedtracker.h"
#include "bitmap.h"

namespace mesh {

using std::atomic_size_t;

bool bitmapsMeshable(const atomic_size_t *__restrict__ bitmap1, const atomic_size_t *__restrict__ bitmap2,
                     size_t len) noexcept;

size_t hammingDistance(const atomic_size_t *__restrict__ bitmap1, const atomic_size_t *__restrict__ bitmap2,
                       size_t len) noexcept;

static constexpr double OccupancyCutoff = .8;

namespace method {

template <typename Heap, typename T>
inline ssize_t simple(const vector<Bitmap<T>> &bitmaps) noexcept {
  if (bitmaps.size() == 0)
    return 0;

  auto meshes = 0;
  const auto len = bitmaps[0].byteCount();

  for (size_t i = 0; i + 1 < bitmaps.size(); i += 2) {
    const auto bitmap1 = bitmaps[i].bitmap();
    const auto bitmap2 = bitmaps[i + 1].bitmap();

    if (bitmapsMeshable(bitmap1, bitmap2, len))
      meshes++;
  }

  return meshes;
}

template <typename T, typename U>
inline void randomSort(mt19937_64 &prng, BinnedTracker<T, U> &miniheaps,
                       const function<void(internal::vector<T *> &&)> &meshFound) noexcept {
  constexpr double OccupancyMaxThreshold = .9;

  internal::vector<T *> heaps{};  // mutable copy

  for (auto mh = miniheaps; mh != nullptr; mh = mh->next()) {
    if (mh->isMeshingCandidate() && mh->fullness() < OccupancyMaxThreshold)
      heaps.push_back(mh);
  }

  auto begin = heaps.begin();
  auto end = heaps.end();

  if (std::distance(begin, end) <= 1)
    return;

  // chose a random permutation of same-sized MiniHeaps
  std::shuffle(begin, end, prng);
  for (auto it1 = begin, it2 = ++begin; it1 != end && it2 != end; ++++it1, ++++it2) {
    T *h1 = *it1;
    T *h2 = *it2;

    d_assert(!h1->isAttached());
    d_assert(!h2->isAttached());

    const auto len = h1->bitmap().byteCount();
    const auto bitmap1 = h1->bitmap().bitmap();
    const auto bitmap2 = h2->bitmap().bitmap();
    const auto len2 = h2->bitmap().byteCount();
    d_assert_msg(len == h2->bitmap().byteCount(), "mismatched lengths? %zu != %zu", len, len2);

    if (mesh::bitmapsMeshable(bitmap1, bitmap2, len)) {
      internal::vector<T *> heaps{h1, h2};
      // debug("----\n2 MESHABLE HEAPS!\n");

      meshFound(std::move(heaps));
    } else {
      // debug("----\n2 UNMESHABLE HEAPS!\n");
      // h1->dumpDebug();
      // h2->dumpDebug();
      // debug("----\n");
    }
  }
}

inline internal::Bitmap splitString(const size_t nBits) noexcept {
  internal::Bitmap bitmap{nBits};

  // set the bottom half of the bits to true
  for (size_t i = 0; i < nBits / 2; i++) {
    bitmap.tryToSet(i);
  }

  return bitmap;
}

class CutoffTable {
private:
  DISALLOW_COPY_AND_ASSIGN(CutoffTable);

public:
  CutoffTable(size_t len, double cutoffPercent) : _len(len), _cutoffPercent(cutoffPercent) {
    d_assert(len <= 256);
    d_assert(cutoffPercent > 0 && cutoffPercent <= 1);

    // size_t cutoff = (size_t)ceil(cutoffPercent * len);

    for (size_t i = 0; i < len; i++) {
      _table[i] = len - 1;

      // TODO: could make this len/2, becuase choose is symmetric
      for (size_t j = 0; j < len; j++) {
        if (CutoffTable::fasterQ(len, i, j) < cutoffPercent) {
          _table[i] = j;
          break;
        }
      }
    }
  }

  size_t get(size_t n) const {
    return _table[n];
  }

private:
  static constexpr double fasterQ(const size_t len, const size_t occ1, const size_t occ2) noexcept {
    size_t numerator = 1;
    for (size_t i = len - occ1; i > len - occ1 - occ2; --i) {
      numerator *= i;
    }

    size_t denominator = 1;
    for (size_t i = len; i > len - occ2; --i) {
      denominator *= i;
    }

    return static_cast<double>(numerator) / static_cast<double>(denominator);
  }

  size_t _len;
  double _cutoffPercent;
  uint8_t _table[];
};

inline CutoffTable *generateCutoffs(const size_t len, const double cutoffPercent) noexcept {
  d_assert(len <= 256);
  static_assert(sizeof(CutoffTable) == sizeof(double) + sizeof(size_t), "CutoffTable: expected packed size");

  void *buf = internal::Heap().malloc(sizeof(CutoffTable) + len * sizeof(uint8_t));

  return new (buf) CutoffTable(len, cutoffPercent);
}

// split miniheaps into two lists depending on if their bitmaps are
// left-heavy or right-heavy
template <typename T, typename U>
inline void unbalancedSplit(mt19937_64 &prng, BinnedTracker<T, U> &miniheaps, internal::vector<T *> &left,
                            internal::vector<T *> &right) noexcept {
  const size_t nBits = miniheaps.objectCount();
  const size_t halfBits = nBits / 2;

  const auto splitStr = splitString(nBits);
  const auto splitBitmap = splitStr.bitmap();

  internal::vector<T *> bucket = miniheaps.meshingCandidates(OccupancyCutoff);
  for (size_t i = 0; i < bucket.size(); i++) {
    auto mh = bucket[i];
    if (!mh->isMeshingCandidate() || mh->fullness() >= OccupancyCutoff)
      continue;

    const auto mhBitmap = mh->bitmap().bitmap();
    const auto distance = mesh::hammingDistance(splitBitmap, mhBitmap, nBits);
    if (distance > halfBits) {
      right.push_back(mh);
    } else if (distance == halfBits) {
      // flip coin
      if (prng() & 1)
        right.push_back(mh);
      else
        left.push_back(mh);
    } else {
      left.push_back(mh);
    }
  }
}

template <typename T, typename U>
inline void simpleGreedySplitting(mt19937_64 &prng, BinnedTracker<T, U> &miniheaps,
                                  const function<void(internal::vector<T *> &&)> &meshFound) noexcept {
  if (miniheaps.partialSize() == 0)
    return;

  internal::vector<T *> bucket = miniheaps.meshingCandidates(OccupancyCutoff);
  // ensure we have a non-zero count
  if (bucket.size() == 0)
    return;

  const size_t nBytes = bucket[0]->bitmap().byteCount();

  std::sort(bucket.begin(), bucket.end());
  // mesh::debug("%zu looking for meshes in %zu MiniHeaps (first fullness %f)", nBytes, bucket.size(),
  //             bucket[0]->fullness());

  const auto candidateCount = bucket.size();
  for (size_t i = 0; i < candidateCount; i++) {
    if (bucket[i] == nullptr)
      continue;

    for (size_t j = i + 1; j < candidateCount; j++) {
      // if we've already meshed a miniheap at this position in the
      // list, this will be null.
      if (bucket[j] == nullptr)
        continue;

      auto h1 = bucket[i];
      auto h2 = bucket[j];

      const auto bitmap1 = h1->bitmap().bitmap();
      const auto bitmap2 = h2->bitmap().bitmap();

      if (mesh::bitmapsMeshable(bitmap1, bitmap2, nBytes)) {
        internal::vector<T *> heaps{h1, h2};
        meshFound(std::move(heaps));
        bucket[j] = nullptr;
        break;  // break after finding a mesh
      }
    }
  }
}

template <typename T, typename U>
inline void greedySplitting(mt19937_64 &prng, BinnedTracker<T, U> &miniheaps,
                            const function<void(internal::vector<T *> &&)> &meshFound) noexcept {
  if (miniheaps.partialSize() == 0)
    return;

  const size_t nBits = miniheaps.objectCount();
  const size_t nBytes = nBits / 8;

  internal::vector<T *> leftBucket{};   // mutable copy
  internal::vector<T *> rightBucket{};  // mutable copy

  unbalancedSplit(prng, miniheaps, leftBucket, rightBucket);

  std::sort(leftBucket.begin(), leftBucket.end());
  std::sort(rightBucket.begin(), rightBucket.end());

  for (size_t i = 0; i < leftBucket.size(); i++) {
    for (size_t j = 0; j < rightBucket.size(); j++) {
      // if we've already meshed a miniheap at this position in the
      // list, this will be null.
      if (rightBucket[j] == nullptr)
        continue;

      auto h1 = leftBucket[i];
      auto h2 = rightBucket[j];

      const auto bitmap1 = h1->bitmap().bitmap();
      const auto bitmap2 = h2->bitmap().bitmap();

      if (mesh::bitmapsMeshable(bitmap1, bitmap2, nBytes)) {
        internal::vector<T *> heaps{h1, h2};
        meshFound(std::move(heaps));
        rightBucket[j] = nullptr;
        break;  // break after finding a mesh
      }
    }
  }
}
}  // namespace method
}  // namespace mesh

#endif  // MESH__MESHING_H
