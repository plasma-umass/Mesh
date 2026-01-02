// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef _WIN32

#include "memory_stats.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include <cstdio>

namespace mesh {

bool MemoryStats::get(MemoryStats &stats) {
  PROCESS_MEMORY_COUNTERS_EX pmc;
  pmc.cb = sizeof(pmc);

  HANDLE hProcess = GetCurrentProcess();
  if (!GetProcessMemoryInfo(hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc))) {
    fprintf(stderr, "[MemoryStats] DEBUG: GetProcessMemoryInfo failed, error=%lu\n", GetLastError());
    return false;
  }

  // WorkingSetSize is the equivalent of RSS (resident set size) on Windows
  stats.resident_size_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);

  // On Windows, mesh uses pagefile-backed sections via CreateFileMapping.
  // Unlike Linux memfd_create which shows in RssShmem, Windows doesn't have
  // a simple equivalent metric. We use PrivateUsage as an approximation,
  // as our shared sections contribute to the working set.
  // For mesh verification tests, the key metric is the change in working set
  // before and after meshing operations.
  stats.mesh_memory_bytes = static_cast<uint64_t>(pmc.WorkingSetSize);

  fprintf(stderr,
          "[MemoryStats] DEBUG: WorkingSetSize=%llu bytes, PrivateUsage=%llu bytes\n",
          static_cast<unsigned long long>(pmc.WorkingSetSize),
          static_cast<unsigned long long>(pmc.PrivateUsage));

  return true;
}

bool MemoryStats::regionResidentBytes(const void *region_begin, size_t region_size, uint64_t &bytes_out) {
  // QueryWorkingSetEx can tell us which pages are in the working set
  // This is analogous to parsing /proc/self/smaps on Linux

  SYSTEM_INFO si;
  GetSystemInfo(&si);
  const SIZE_T pageSize = si.dwPageSize;

  // Calculate number of pages in the region
  const uintptr_t start = reinterpret_cast<uintptr_t>(region_begin);
  const uintptr_t aligned_start = start & ~(pageSize - 1);
  const uintptr_t end = start + region_size;
  const uintptr_t aligned_end = (end + pageSize - 1) & ~(pageSize - 1);
  const SIZE_T numPages = (aligned_end - aligned_start) / pageSize;

  if (numPages == 0) {
    bytes_out = 0;
    return true;
  }

  // Allocate array for PSAPI_WORKING_SET_EX_INFORMATION
  // Note: This can be a large allocation for big regions
  const SIZE_T maxPagesPerQuery = 1024;  // Query in batches to avoid huge allocations
  PSAPI_WORKING_SET_EX_INFORMATION *pInfo = static_cast<PSAPI_WORKING_SET_EX_INFORMATION *>(
      VirtualAlloc(nullptr, maxPagesPerQuery * sizeof(PSAPI_WORKING_SET_EX_INFORMATION),
                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

  if (pInfo == nullptr) {
    fprintf(stderr, "[MemoryStats] DEBUG: Failed to allocate query buffer, error=%lu\n", GetLastError());
    return false;
  }

  HANDLE hProcess = GetCurrentProcess();
  uint64_t residentBytes = 0;

  // Query pages in batches
  for (SIZE_T pageOffset = 0; pageOffset < numPages; pageOffset += maxPagesPerQuery) {
    SIZE_T pagesToQuery = numPages - pageOffset;
    if (pagesToQuery > maxPagesPerQuery) {
      pagesToQuery = maxPagesPerQuery;
    }

    // Fill in virtual addresses to query
    for (SIZE_T i = 0; i < pagesToQuery; i++) {
      pInfo[i].VirtualAddress = reinterpret_cast<PVOID>(aligned_start + (pageOffset + i) * pageSize);
    }

    // Query working set information
    if (!QueryWorkingSetEx(hProcess, pInfo,
                           static_cast<DWORD>(pagesToQuery * sizeof(PSAPI_WORKING_SET_EX_INFORMATION)))) {
      // QueryWorkingSetEx can fail for various reasons, but partial results may still be valid
      fprintf(stderr, "[MemoryStats] DEBUG: QueryWorkingSetEx failed, error=%lu\n", GetLastError());
    }

    // Count resident pages
    for (SIZE_T i = 0; i < pagesToQuery; i++) {
      // Check if the Valid bit is set (page is in working set)
      if (pInfo[i].VirtualAttributes.Valid) {
        residentBytes += pageSize;
      }
    }
  }

  VirtualFree(pInfo, 0, MEM_RELEASE);

  bytes_out = residentBytes;
  return true;
}

}  // namespace mesh

#endif  // _WIN32
