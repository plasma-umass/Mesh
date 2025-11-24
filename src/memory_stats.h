// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH__MEMORY_STATS_H
#define MESH__MEMORY_STATS_H

#include <cstddef>
#include <cstdint>

namespace mesh {

// Platform-independent memory statistics interface.
// Provides a minimal, focused API for querying memory metrics needed
// to verify meshing behavior in tests.
struct MemoryStats {
  // Total resident set size in bytes
  uint64_t resident_size_bytes;

  // Platform-specific metric for mesh's memory-mapped pages:
  // - Linux: RssShmem (shared memory from memfd_create)
  // - macOS: RSS (since MAP_SHARED shows in RSS)
  // This is the key metric for verifying meshing reduces physical memory.
  uint64_t mesh_memory_bytes;

  // Get current memory statistics for this process.
  // Returns true on success, false on failure.
  static bool get(MemoryStats &stats);

  // Best-effort resident byte count for a specific virtual address range.
  // - Linux: parsed from /proc/self/smaps (Rss field)
  // - macOS: falls back to mesh_memory_bytes (process RSS) as a coarse proxy
  // Returns true on success, false on failure.
  static bool regionResidentBytes(const void *region_begin, size_t region_size, uint64_t &bytes_out);
};

}  // namespace mesh

#endif  // MESH__MEMORY_STATS_H
