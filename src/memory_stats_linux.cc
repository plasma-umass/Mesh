// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef __linux__

#include "memory_stats.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <cerrno>
#include <cstdio>
#include <sys/types.h>

namespace mesh {

bool MemoryStats::get(MemoryStats &stats) {
  // Read /proc/self/status to get memory metrics
  constexpr size_t kBufLen = 4096;
  char buf[kBufLen] = {0};

  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "[MemoryStats] DEBUG: Failed to open /proc/self/status, errno=%d\n", errno);
    return false;
  }

  ssize_t bytes_read = read(fd, buf, kBufLen - 1);
  close(fd);

  if (bytes_read <= 0) {
    fprintf(stderr, "[MemoryStats] DEBUG: Failed to read /proc/self/status, bytes_read=%zd\n", bytes_read);
    return false;
  }

  fprintf(stderr, "[MemoryStats] DEBUG: Successfully read %zd bytes from /proc/self/status\n", bytes_read);

  // Initialize to detect if we found the values
  long rss_kb = -1;
  long rss_shmem_kb = -1;
  long rss_anon_kb = -1;
  long rss_file_kb = -1;
  bool found_rss_line = false;
  bool found_rss_shmem_line = false;
  bool found_rss_anon_line = false;
  bool found_rss_file_line = false;

  // Parse VmRSS and RssShmem lines
  for (char *line = buf; line != nullptr && *line != '\0'; line = strchr(line, '\n')) {
    if (*line == '\n') {
      line++;
    }

    if (strncmp(line, "VmRSS:", 6) == 0) {
      found_rss_line = true;
      char *str = line + 6;
      // Skip whitespace
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_kb = strtol(str, nullptr, 10);
      fprintf(stderr, "[MemoryStats] DEBUG: Found VmRSS line, value=%ld KB\n", rss_kb);
    } else if (strncmp(line, "RssShmem:", 9) == 0) {
      found_rss_shmem_line = true;
      char *str = line + 9;
      // Skip whitespace
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_shmem_kb = strtol(str, nullptr, 10);
      fprintf(stderr, "[MemoryStats] DEBUG: Found RssShmem line, value=%ld KB\n", rss_shmem_kb);
    } else if (strncmp(line, "RssAnon:", 8) == 0) {
      found_rss_anon_line = true;
      char *str = line + 8;
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_anon_kb = strtol(str, nullptr, 10);
      fprintf(stderr, "[MemoryStats] DEBUG: Found RssAnon line, value=%ld KB\n", rss_anon_kb);
    } else if (strncmp(line, "RssFile:", 8) == 0) {
      found_rss_file_line = true;
      char *str = line + 8;
      while (*str == ' ' || *str == '\t') {
        str++;
      }
      rss_file_kb = strtol(str, nullptr, 10);
      fprintf(stderr, "[MemoryStats] DEBUG: Found RssFile line, value=%ld KB\n", rss_file_kb);
    }

    // Stop if we found both values
    if (rss_kb >= 0 && rss_shmem_kb >= 0 && rss_anon_kb >= 0 && rss_file_kb >= 0) {
      break;
    }
  }

  // Log what we found or didn't find
  if (!found_rss_line) {
    fprintf(stderr, "[MemoryStats] DEBUG: VmRSS line NOT found in /proc/self/status\n");
  }
  if (!found_rss_shmem_line) {
    fprintf(stderr, "[MemoryStats] DEBUG: RssShmem line NOT found in /proc/self/status (kernel may not support it)\n");
  }
  if (!found_rss_anon_line) {
    fprintf(stderr, "[MemoryStats] DEBUG: RssAnon line NOT found in /proc/self/status\n");
  }
  if (!found_rss_file_line) {
    fprintf(stderr, "[MemoryStats] DEBUG: RssFile line NOT found in /proc/self/status\n");
  }

  // Check we found both values
  if (rss_kb < 0 || rss_shmem_kb < 0) {
    fprintf(stderr, "[MemoryStats] DEBUG: Missing required values - VmRSS=%ld KB, RssShmem=%ld KB\n", rss_kb,
            rss_shmem_kb);
    return false;
  }

  stats.resident_size_bytes = static_cast<uint64_t>(rss_kb) * 1024;
  // On Linux, mesh's memfd_create memory shows up in RssShmem
  stats.mesh_memory_bytes = static_cast<uint64_t>(rss_shmem_kb) * 1024;

  fprintf(stderr,
          "[MemoryStats] DEBUG: Successfully parsed - RSS=%llu bytes, mesh_memory=%llu bytes, RssAnon=%ld KB, "
          "RssFile=%ld KB\n",
          static_cast<unsigned long long>(stats.resident_size_bytes),
          static_cast<unsigned long long>(stats.mesh_memory_bytes), rss_anon_kb, rss_file_kb);

  return true;
}

bool MemoryStats::regionResidentBytes(const void *region_begin, size_t region_size, uint64_t &bytes_out) {
  FILE *fp = fopen("/proc/self/smaps", "r");
  if (!fp) {
    return false;
  }

  const uintptr_t target_start = reinterpret_cast<uintptr_t>(region_begin);
  const uintptr_t target_end = target_start + region_size;

  uint64_t rss_bytes = 0;
  bool in_entry = false;
  uintptr_t entry_start = 0;
  uintptr_t entry_end = 0;

  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    // Mapping header line: start-end perms ...
    if (sscanf(line, "%lx-%lx", &entry_start, &entry_end) == 2) {
      // Start a new entry; determine if it overlaps our region.
      in_entry = (entry_start < target_end) && (entry_end > target_start);
      continue;
    }

    if (!in_entry) {
      continue;
    }

    // Rss: <value> kB
    if (strncmp(line, "Rss:", 4) == 0) {
      unsigned long rss_kb = 0;
      if (sscanf(line + 4, "%lu", &rss_kb) == 1) {
        rss_bytes += static_cast<uint64_t>(rss_kb) * 1024;
      }
    }
  }

  fclose(fp);
  bytes_out = rss_bytes;
  return true;
}

}  // namespace mesh

#endif  // __linux__
