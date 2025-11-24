// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __APPLE__

#include "memory_stats_macos.h"
#include "memory_stats.h"
#include <unistd.h>
#include <stdio.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach/vm_region.h>
#include <sys/mman.h>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace mesh {

bool MacOSMemoryStats::get(MacOSMemoryStats &stats) {
  task_vm_info_data_t vm_info;
  mach_msg_type_number_t vm_size = TASK_VM_INFO_COUNT;

  kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm_info), &vm_size);

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

  kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&vm_info), &vm_size);

  if (kr != KERN_SUCCESS) {
    return 0;
  }

  return vm_info.phys_footprint;
}

int get_footprint_kb() {
  return static_cast<int>(get_footprint_bytes() / 1024);
}

// Unified interface implementation
bool MemoryStats::get(MemoryStats &stats) {
  MacOSMemoryStats macos_stats;
  if (!MacOSMemoryStats::get(macos_stats)) {
    return false;
  }
  // Keep RSS for comparability with Linux process stats
  stats.resident_size_bytes = macos_stats.resident_size;
  // On macOS, MAP_SHARED memory (used by mesh) shows up in RSS.
  stats.mesh_memory_bytes = macos_stats.resident_size;
  return true;
}

bool MemoryStats::regionResidentBytes(const void *region_begin, size_t region_size, uint64_t &bytes_out) {
  struct PageKey {
    uint64_t object;
    uint64_t offset;
    uint32_t depth;
    bool operator==(const PageKey &other) const {
      return object == other.object && offset == other.offset && depth == other.depth;
    }
  };

  struct PageKeyHash {
    size_t operator()(const PageKey &k) const {
      size_t h1 = std::hash<uint64_t>{}(k.object);
      size_t h2 = std::hash<uint64_t>{}(k.offset);
      size_t h3 = std::hash<uint32_t>{}(k.depth);
      size_t combined = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
      return combined ^ (h3 + 0x9e3779b97f4a7c15ULL + (combined << 6) + (combined >> 2));
    }
  };

  const size_t page_size = static_cast<size_t>(getpagesize());
  const mach_vm_address_t start = reinterpret_cast<mach_vm_address_t>(region_begin);
  const mach_vm_address_t page_aligned_start = start & ~(static_cast<mach_vm_address_t>(page_size - 1));
  const size_t offset = static_cast<size_t>(start - page_aligned_start);
  const size_t length = region_size + offset;
  const size_t page_count = (length + page_size - 1) / page_size;

  std::unordered_set<PageKey, PageKeyHash> unique_pages;

  const bool debug = getenv("MESH_DEBUG_PAGEINFO") != nullptr;
  static std::atomic<uint64_t> debug_call_id{0};
  const uint64_t call_id = debug ? debug_call_id.fetch_add(1, std::memory_order_relaxed) : 0;

  for (size_t i = 0; i < page_count; i++) {
    const mach_vm_address_t addr = page_aligned_start + static_cast<mach_vm_address_t>(i * page_size);
    vm_page_info_basic_data_t info{};
    mach_msg_type_number_t info_count = VM_PAGE_INFO_BASIC_COUNT;

    kern_return_t kr = mach_vm_page_info(mach_task_self(), addr, VM_PAGE_INFO_BASIC,
                                         reinterpret_cast<vm_page_info_t>(&info), &info_count);
    if (kr != KERN_SUCCESS || info_count < VM_PAGE_INFO_BASIC_COUNT) {
      if (debug) {
        fprintf(stderr, "[pageinfo %llu] mach_vm_page_info failed at addr 0x%llx: kr=%d count=%u\n",
                static_cast<unsigned long long>(call_id), static_cast<unsigned long long>(addr), kr, info_count);
      }
      return false;
    }

    // disposition == 0 indicates an unmapped hole; skip it. Otherwise count resident pages.
    if (info.disposition == 0) {
      if (debug) {
        fprintf(stderr, "[pageinfo %llu] addr 0x%llx: disposition=0 object=0x%llx offset=0x%llx depth=%d\n",
                static_cast<unsigned long long>(call_id), static_cast<unsigned long long>(addr),
                static_cast<unsigned long long>(info.object_id), static_cast<unsigned long long>(info.offset),
                info.depth);
      }
      continue;
    }

    if (debug) {
      fprintf(stderr, "[pageinfo %llu] addr 0x%llx: disposition=%d object=0x%llx offset=0x%llx depth=%d\n",
              static_cast<unsigned long long>(call_id), static_cast<unsigned long long>(addr), info.disposition,
              static_cast<unsigned long long>(info.object_id), static_cast<unsigned long long>(info.offset),
              info.depth);
    }

    unique_pages.insert(PageKey{static_cast<uint64_t>(info.object_id), static_cast<uint64_t>(info.offset),
                                static_cast<uint32_t>(info.depth)});
  }

  bytes_out = unique_pages.size() * page_size;
  return true;
}

}  // namespace mesh

#endif  // __APPLE__
