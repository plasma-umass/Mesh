// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __APPLE__

#include "memory_stats_macos.h"
#include "memory_stats.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <vector>
#include <cstdint>

namespace mesh {

bool MacOSMemoryStats::get(MacOSMemoryStats& stats) {
  task_vm_info_data_t vm_info;
  mach_msg_type_number_t vm_size = TASK_VM_INFO_COUNT;

  kern_return_t kr = task_info(mach_task_self(),
                                TASK_VM_INFO,
                                reinterpret_cast<task_info_t>(&vm_info),
                                &vm_size);

  if (kr != KERN_SUCCESS) {
    return false;
  }

  stats.physical_footprint = vm_info.phys_footprint;
  stats.resident_size = vm_info.resident_size;
  stats.virtual_size = vm_info.virtual_size;
  stats.compressed = vm_info.compressed;
  stats.purgeable = vm_info.purgeable_volatile_pmap;

  // Calculate dirty pages (approximation)
  stats.dirty = vm_info.internal - vm_info.purgeable_volatile_pmap;

  return true;
}

void MacOSMemoryStats::print() const {
  printf("macOS Memory Stats:\n");
  printf("  Physical Footprint: %.2f MB (most accurate)\n", physical_footprint / (1024.0 * 1024.0));
  printf("  Resident Size (RSS): %.2f MB\n", resident_size / (1024.0 * 1024.0));
  printf("  Virtual Size: %.2f MB\n", virtual_size / (1024.0 * 1024.0));
  printf("  Compressed: %.2f MB\n", compressed / (1024.0 * 1024.0));
  printf("  Dirty: %.2f MB\n", dirty / (1024.0 * 1024.0));
  printf("  Purgeable: %.2f MB\n", purgeable / (1024.0 * 1024.0));
}

uint64_t get_footprint_bytes() {
  task_vm_info_data_t vm_info;
  mach_msg_type_number_t vm_size = TASK_VM_INFO_COUNT;

  kern_return_t kr = task_info(mach_task_self(),
                                TASK_VM_INFO,
                                reinterpret_cast<task_info_t>(&vm_info),
                                &vm_size);

  if (kr != KERN_SUCCESS) {
    return 0;
  }

  return vm_info.phys_footprint;
}

int get_footprint_kb() {
  return static_cast<int>(get_footprint_bytes() / 1024);
}

// Unified interface implementation
bool MemoryStats::get(MemoryStats& stats) {
  MacOSMemoryStats macos_stats;
  if (!MacOSMemoryStats::get(macos_stats)) {
    return false;
  }
  // Use RSS as the primary metric for cross-platform consistency
  stats.resident_size_bytes = macos_stats.resident_size;
  // On macOS, MAP_SHARED memory (used by mesh) shows up in RSS
  stats.mesh_memory_bytes = macos_stats.resident_size;
  return true;
}

bool MemoryStats::regionResidentBytes(const void* region_begin, size_t region_size, uint64_t& bytes_out) {
  const size_t page_size = static_cast<size_t>(getpagesize());
  const uintptr_t start = reinterpret_cast<uintptr_t>(region_begin);
  const uintptr_t page_aligned_start = start & ~(static_cast<uintptr_t>(page_size) - 1);
  const size_t offset = start - page_aligned_start;
  const size_t length = region_size + offset;
  const size_t page_count = (length + page_size - 1) / page_size;

  std::vector<unsigned char> residency(page_count);
  if (mincore(reinterpret_cast<void*>(page_aligned_start), page_count * page_size,
              reinterpret_cast<char*>(residency.data())) != 0) {
    return false;
  }

  uint64_t resident_bytes = 0;
  for (size_t i = 0; i < page_count; i++) {
    if (residency[i] & 0x1) {
      // count full pages; test expects full-page allocations
      resident_bytes += page_size;
    }
  }

  bytes_out = resident_bytes;
  return true;
}

}  // namespace mesh

#endif  // __APPLE__
