// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MESHING_H
#define MESH__MESHING_H

#include "common.h"

#include "bitmap.h"

namespace mesh {

inline bool bitmapsMeshable(const uint64_t *bitmap1, const uint64_t *bitmap2, size_t len) {
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
inline ssize_t simple(const vector<Bitmap<T>> &bitmaps) {
  if (bitmaps.size() == 0)
    return 0;

  auto meshes = 0;
  auto len = bitmaps[0].wordCount() / 8; // 8 words per 64-bit int

  for (size_t i = 0; i < bitmaps.size()-1; i += 2) {
    const auto bitmap1 = bitmaps[i].bitmap();
    const auto bitmap2 = bitmaps[i+1].bitmap();

    if (bitmapsMeshable(bitmap1, bitmap2, len))
      meshes++;
  }

  return meshes;
}
}
}

#endif  // MESH__MESHING_H
