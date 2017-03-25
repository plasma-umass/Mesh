// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include "meshing.h"

namespace mesh {

bool bitmapsMeshable(const atomic_size_t *__restrict__ bitmap1, const atomic_size_t *__restrict__ bitmap2,
                     size_t len) noexcept {
  // d_assert(reinterpret_cast<uintptr_t>(bitmap1) % 16 == 0);
  // d_assert(reinterpret_cast<uintptr_t>(bitmap2) % 16 == 0);
  d_assert(len >= 8);
  d_assert(len % 8 == 0);

  bitmap1 = (const atomic_size_t *)__builtin_assume_aligned(bitmap1, 16);
  bitmap2 = (const atomic_size_t *)__builtin_assume_aligned(bitmap2, 16);

  for (size_t i = 0; i < len / 8; i++) {
    if ((bitmap1[i] & bitmap2[i]) != 0) {
      // debug("%zu/%zu bitmap cmp failed: %zx & %zx != 0 (%zx)", i, len, bitmap1[i].load(), bitmap2[i].load(),
      // bitmap1[i].load() & bitmap2[i].load());
      return false;
    }
  }
  return true;
}
}  // namespace mesh
