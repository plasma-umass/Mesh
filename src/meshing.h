// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MESHING_H
#define MESH__MESHING_H

#include <algorithm>
#include <atomic>

#include "common.h"

#include "internal.h"
#include "miniheap.h"

#include "bitmap.h"

namespace mesh {

using std::atomic_size_t;

bool bitmapsMeshable(const atomic_size_t *__restrict__ bitmap1, const atomic_size_t *__restrict__ bitmap2,
                     size_t len) noexcept;

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

template <typename T>
inline void randomSort(mt19937_64 &prng, size_t count, T *miniheaps,
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
}  // namespace method
}  // namespace mesh

#endif  // MESH__MESHING_H
