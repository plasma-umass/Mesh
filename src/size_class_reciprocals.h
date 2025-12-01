// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_SIZE_CLASS_RECIPROCALS_H
#define MESH_SIZE_CLASS_RECIPROCALS_H

#include <cstddef>
#include <cstdint>

#ifndef ATTRIBUTE_ALWAYS_INLINE
#define ATTRIBUTE_ALWAYS_INLINE __attribute__((always_inline))
#endif

namespace mesh {

// Number of size classes - defined in common.h when included from production
// code, but may be defined standalone for benchmarks
#ifndef MESH_COMMON_H
static constexpr size_t kClassSizesMax = 25;
#endif

// Size classes from runtime.cc - must stay in sync
// {16, 16, 32, 48, 64, 80, 96, 112, 128, 160, 192, 224, 256,
//  320, 384, 448, 512, 640, 768, 896, 1024, 2048, 4096, 8192, 16384}

// Float reciprocal table for computing object index from byte offset.
// For each size class, stores 1.0f / object_size.
// This is the production approach used by MiniHeap.
namespace float_recip {

inline constexpr float kReciprocals[kClassSizesMax] = {
    1.0f / 16.0f,     // class 0: 16 bytes
    1.0f / 16.0f,     // class 1: 16 bytes
    1.0f / 32.0f,     // class 2: 32 bytes
    1.0f / 48.0f,     // class 3: 48 bytes
    1.0f / 64.0f,     // class 4: 64 bytes
    1.0f / 80.0f,     // class 5: 80 bytes
    1.0f / 96.0f,     // class 6: 96 bytes
    1.0f / 112.0f,    // class 7: 112 bytes
    1.0f / 128.0f,    // class 8: 128 bytes
    1.0f / 160.0f,    // class 9: 160 bytes
    1.0f / 192.0f,    // class 10: 192 bytes
    1.0f / 224.0f,    // class 11: 224 bytes
    1.0f / 256.0f,    // class 12: 256 bytes
    1.0f / 320.0f,    // class 13: 320 bytes
    1.0f / 384.0f,    // class 14: 384 bytes
    1.0f / 448.0f,    // class 15: 448 bytes
    1.0f / 512.0f,    // class 16: 512 bytes
    1.0f / 640.0f,    // class 17: 640 bytes
    1.0f / 768.0f,    // class 18: 768 bytes
    1.0f / 896.0f,    // class 19: 896 bytes
    1.0f / 1024.0f,   // class 20: 1024 bytes
    1.0f / 2048.0f,   // class 21: 2048 bytes
    1.0f / 4096.0f,   // class 22: 4096 bytes
    1.0f / 8192.0f,   // class 23: 8192 bytes
    1.0f / 16384.0f,  // class 24: 16384 bytes
};

// Compute object index from byte offset using float reciprocal
inline size_t ATTRIBUTE_ALWAYS_INLINE computeIndex(size_t byteOffset, uint32_t sizeClass) {
  return static_cast<size_t>(static_cast<float>(byteOffset) * kReciprocals[sizeClass]);
}

// Get reciprocal for a size class
inline float ATTRIBUTE_ALWAYS_INLINE getReciprocal(uint32_t sizeClass) {
  return kReciprocals[sizeClass];
}

}  // namespace float_recip

}  // namespace mesh

#endif  // MESH_SIZE_CLASS_RECIPROCALS_H
