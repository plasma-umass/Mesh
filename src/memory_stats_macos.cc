// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __APPLE__

#include "memory_stats_macos.h"
#include <unistd.h>
#include <stdio.h>

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

}  // namespace mesh

#endif  // __APPLE__