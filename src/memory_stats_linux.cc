// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __linux__

#include "memory_stats.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

namespace mesh {

bool MemoryStats::get(MemoryStats& stats) {
  // Read /proc/self/status to get memory metrics
  constexpr size_t kBufLen = 4096;
  char buf[kBufLen] = {0};

  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    return false;
  }

  ssize_t bytes_read = read(fd, buf, kBufLen - 1);
  close(fd);

  if (bytes_read <= 0) {
    return false;
  }

  // Initialize to detect if we found the values
  long rss_kb = -1;
  long rss_shmem_kb = -1;

  // Parse VmRSS and RssShmem lines
  for (char* line = buf; line != nullptr && *line != '\0'; line = strchr(line, '\n')) {
    if (*line == '\n') {
      line++;
    }

    if (strncmp(line, "VmRSS:", 6) == 0) {
      char* str = line + 6;
      // Skip whitespace
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_kb = strtol(str, nullptr, 10);
    } else if (strncmp(line, "RssShmem:", 9) == 0) {
      char* str = line + 9;
      // Skip whitespace
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_shmem_kb = strtol(str, nullptr, 10);
    }

    // Stop if we found both values
    if (rss_kb >= 0 && rss_shmem_kb >= 0) {
      break;
    }
  }

  // Check we found both values
  if (rss_kb < 0 || rss_shmem_kb < 0) {
    return false;
  }

  stats.resident_size_bytes = static_cast<uint64_t>(rss_kb) * 1024;
  // On Linux, mesh's memfd_create memory shows up in RssShmem
  stats.mesh_memory_bytes = static_cast<uint64_t>(rss_shmem_kb) * 1024;
  return true;
}

}  // namespace mesh

#endif  // __linux__