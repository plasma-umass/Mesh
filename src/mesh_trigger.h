// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_TRIGGER_H
#define MESH_TRIGGER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "common.h"

namespace mesh {
namespace internal {

// Lightweight per-size-class meshing trigger. Tracks freed bytes and uses a
// small exponential backoff when meshing makes no progress to reduce churn.
template <size_t NumClasses>
class MeshTrigger {
public:
  explicit MeshTrigger(const std::array<uint64_t, NumClasses> &baseBudgets) : _baseBudgets(baseBudgets) {
  }

  // Record freed bytes for a size class. When the accumulated value exceeds
  // the (backoff-adjusted) budget, a meshing request is queued.
  inline void add(size_t sizeClass, uint64_t deltaBytes) {
    if (deltaBytes == 0) {
      return;
    }
    d_assert(sizeClass < NumClasses);
    auto &counter = _freeBytes[sizeClass];
    const uint64_t total = counter.fetch_add(deltaBytes, std::memory_order_relaxed) + deltaBytes;
    if (total >= adjustedBudget(sizeClass)) {
      request(sizeClass);
    }
  }

  // Pops a pending meshing request, preferring the provided size class if it
  // is queued. Returns true when a request was found and removed.
  inline bool popRequested(int preferredSizeClass, size_t &outSizeClass) {
    uint32_t mask = _requestedMask.load(std::memory_order_acquire);
    while (mask) {
      size_t candidate = 0;
      if (preferredSizeClass >= 0) {
        const uint32_t bit = 1u << preferredSizeClass;
        if (mask & bit) {
          candidate = static_cast<size_t>(preferredSizeClass);
        } else {
          candidate = static_cast<size_t>(__builtin_ctz(mask));
        }
      } else {
        candidate = static_cast<size_t>(__builtin_ctz(mask));
      }

      const uint32_t bit = 1u << candidate;
      const uint32_t newMask = mask & ~bit;
      if (_requestedMask.compare_exchange_weak(mask, newMask, std::memory_order_acq_rel, std::memory_order_acquire)) {
        outSizeClass = candidate;
        return true;
      }
      // CAS failed; mask now holds the latest value, loop again.
    }
    return false;
  }

  // Update internal counters after a mesh attempt.
  inline void onMeshComplete(size_t sizeClass, bool madeProgress, uint64_t budgetHint = 0) {
    d_assert(sizeClass < NumClasses);
    const uint64_t budget = budgetHint > 0 ? budgetHint : adjustedBudget(sizeClass);

    auto &counter = _freeBytes[sizeClass];
    uint64_t prev = counter.load(std::memory_order_relaxed);
    while (true) {
      const uint64_t newVal = prev > budget ? prev - budget : 0;
      if (counter.compare_exchange_weak(prev, newVal, std::memory_order_release, std::memory_order_relaxed)) {
        break;
      }
    }

    auto &backoff = _backoff[sizeClass];
    if (madeProgress) {
      backoff.store(0, std::memory_order_release);
    } else {
      const uint8_t prevBackoff = backoff.load(std::memory_order_relaxed);
      if (prevBackoff < kMaxBackoffShift) {
        backoff.store(prevBackoff + 1, std::memory_order_release);
      }
    }
  }

  inline uint64_t adjustedBudget(size_t sizeClass) const {
    d_assert(sizeClass < NumClasses);
    const uint8_t shift = _backoff[sizeClass].load(std::memory_order_relaxed);
    const uint64_t base = _baseBudgets[sizeClass];
    if (shift >= kMaxShift) {
      return static_cast<uint64_t>(-1);
    }
    const uint64_t maxShift = static_cast<uint64_t>(kMaxShift);
    const uint64_t clampedShift = shift > maxShift ? maxShift : shift;
    return base << clampedShift;
  }

  inline uint8_t backoff(size_t sizeClass) const {
    d_assert(sizeClass < NumClasses);
    return _backoff[sizeClass].load(std::memory_order_relaxed);
  }

  inline uint64_t pendingBytes(size_t sizeClass) const {
    d_assert(sizeClass < NumClasses);
    return _freeBytes[sizeClass].load(std::memory_order_relaxed);
  }

private:
  inline void request(size_t sizeClass) {
    const uint32_t bit = 1u << sizeClass;
    _requestedMask.fetch_or(bit, std::memory_order_release);
  }

  static constexpr uint8_t kMaxBackoffShift = 6;  // 64x budget at most
  static constexpr uint8_t kMaxShift = 20;

  std::array<std::atomic<uint64_t>, NumClasses> _freeBytes{};
  std::array<std::atomic<uint8_t>, NumClasses> _backoff{};
  std::array<uint64_t, NumClasses> _baseBudgets;
  std::atomic<uint32_t> _requestedMask{0};
};

}  // namespace internal
}  // namespace mesh

#endif  // MESH_TRIGGER_H
