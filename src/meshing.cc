// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "meshing.h"

namespace mesh {

bool bitmapsMeshable(const Bitmap::word_t *__restrict__ bitmap1, const Bitmap::word_t *__restrict__ bitmap2,
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

size_t hammingDistance(const Bitmap::word_t *__restrict__ bitmap1, const Bitmap::word_t *__restrict__ bitmap2,
                       size_t byteLen) noexcept {
  // d_assert(reinterpret_cast<uintptr_t>(bitmap1) % 16 == 0);
  // d_assert(reinterpret_cast<uintptr_t>(bitmap2) % 16 == 0);
  d_assert(byteLen >= 8);
  d_assert(byteLen % 8 == 0);

  bitmap1 = (const Bitmap::word_t *)__builtin_assume_aligned(bitmap1, 16);
  bitmap2 = (const Bitmap::word_t *)__builtin_assume_aligned(bitmap2, 16);

  size_t result = 0;

  for (size_t i = 0; i < byteLen / 8; i++) {
    result += __builtin_popcountl(bitmap1[i] ^ bitmap2[i]);
  }

  return result;
}
}  // namespace mesh
