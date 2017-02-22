// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MESHING_H
#define MESH__MESHING_H

#include "common.h"

#include "internal.h"
#include "miniheap.h"

#include "bitmap.h"

namespace mesh {

inline bool bitmapsMeshable(const uint64_t *bitmap1, const uint64_t *bitmap2, size_t len) noexcept {
  d_assert(reinterpret_cast<uintptr_t>(bitmap1) % 16 == 0);
  d_assert(reinterpret_cast<uintptr_t>(bitmap2) % 16 == 0);

  bitmap1 = (const uint64_t *)__builtin_assume_aligned(bitmap1, 16);
  bitmap2 = (const uint64_t *)__builtin_assume_aligned(bitmap2, 16);

  for (size_t i = 0; i < len; i++) {
    if ((bitmap1[i] & bitmap2[i]) != 0)
      return false;
  }
  return true;
}

namespace method {

template <typename Heap, typename T>
inline ssize_t simple(const vector<Bitmap<T>> &bitmaps) noexcept {
  if (bitmaps.size() == 0)
    return 0;

  auto meshes = 0;
  const auto len = bitmaps[0].wordCount() / 8;  // 8 words per 64-bit int

  for (size_t i = 0; i + 1 < bitmaps.size(); i += 2) {
    const auto bitmap1 = bitmaps[i].bitmap();
    const auto bitmap2 = bitmaps[i + 1].bitmap();

    if (bitmapsMeshable(bitmap1, bitmap2, len))
      meshes++;
  }

  return meshes;
}
}

template <typename T>
inline void randomSort(mt19937_64 &prng, const internal::vector<T *> &miniheaps,
                       const function<void(internal::vector<T *> &&)> &meshFound) noexcept {
  // TODO: filter miniheaps by occupancy
  auto heaps(miniheaps);  // mutable copy
  auto begin = heaps.begin();
  auto end = heaps.end();

  // if the current heap (the one we are allocating out of) isn't
  // done, exclude it from meshing
  if (heaps.size() > 1 && !heaps.back()->isDone())
    --end;

  if (std::distance(begin, end) <= 1)
    return;

  // chose a random permutation of same-sized MiniHeaps
  std::shuffle(begin, end, prng);
  for (auto it1 = begin, it2 = ++begin; it2 != end; ++it1, ++it2) {
    T *h1 = *it1;
    T *h2 = *it2;

    if (h1->isDone() || h2->isDone())
      continue;

    const auto len = h1->bitmap().wordCount();
    const auto bitmap1 = h1->bitmap().bitmap();
    const auto bitmap2 = h2->bitmap().bitmap();
    d_assert(len == h2->bitmap().wordCount());

    if (mesh::bitmapsMeshable(bitmap1, bitmap2, len)) {
      internal::vector<T *> heaps{h1, h2};
      debug("----\n2 MESHABLE HEAPS!\n");

      // h1->dumpDebug();
      // h2->dumpDebug();
      // debug("----\n");

      meshFound(std::move(heaps));
    }
  }
}
}

#endif  // MESH__MESHING_H
