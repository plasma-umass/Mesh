// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once

#ifdef __APPLE__

#include <mach/mach.h>
#include <mach/task_info.h>

namespace mesh {

struct MacOSMemoryStats {
  uint64_t physical_footprint;  // Most accurate metric for actual memory usage
  uint64_t resident_size;       // Traditional RSS
  uint64_t virtual_size;        // Virtual memory size
  uint64_t compressed;          // Compressed memory
  uint64_t dirty;               // Dirty pages
  uint64_t purgeable;           // Purgeable pages

  // Get current memory statistics
  static bool get(MacOSMemoryStats &stats);

  // Print detailed memory stats
  void print() const;

  // Get footprint in KB (main metric for memory usage on macOS)
  uint64_t footprint_kb() const {
    return physical_footprint / 1024;
  }

  // Get RSS in KB (for compatibility)
  uint64_t rss_kb() const {
    return resident_size / 1024;
  }
};

// Get physical footprint in bytes (most accurate memory metric on macOS)
uint64_t get_footprint_bytes();

// Get physical footprint in KB
int get_footprint_kb();

}  // namespace mesh

#endif  // __APPLE__