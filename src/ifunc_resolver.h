// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2024 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifndef MESH__IFUNC_RESOLVER_H
#define MESH__IFUNC_RESOLVER_H

#include "common.h"

#ifdef __linux__
#include <elf.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

// ===================================================================
// IFUNC Resolver Infrastructure
// ===================================================================
//
// This header provides utilities for IFUNC (Indirect Function) resolvers.
// IFUNC resolvers execute during dynamic linking BEFORE any of the following
// are available:
//   - glibc initialization (no malloc, no stdio, no getauxval)
//   - libstdc++ availability (no C++ standard library)
//   - TLS (Thread-Local Storage) setup
//   - Most syscall wrappers (syscall() function not available)
//   - Any library functions whatsoever
//
// The resolver runs in an extremely restricted environment where we MUST:
//   - Parse auxv (auxiliary vector) directly from /proc/self/auxv
//   - Use ONLY raw syscalls via inline assembly (no libc wrappers)
//   - Avoid ALL library function calls
//   - Keep logic minimal and completely self-contained
//   - Not use any global variables (they may not be initialized)
//
// This is why we need raw inline assembly for syscalls and manual parsing
// of the auxiliary vector.
// ===================================================================

namespace mesh {
namespace ifunc {

#ifdef __aarch64__
// ===================================================================
// ARM64 Syscall Implementation
// ===================================================================
// ARM64 inline assembly for system calls - REQUIRED because syscall()
// wrapper is not available during IFUNC resolution.
//
// ARM64 calling convention for syscalls:
//   - Syscall number goes in x8 register
//   - Arguments go in x0-x5 registers
//   - Return value comes back in x0
//   - SVC #0 instruction triggers the syscall
// ===================================================================

__attribute__((no_stack_protector)) inline long syscall_3(long nr, long arg0, long arg1, long arg2) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = arg0;
  register long x1 __asm__("x1") = arg1;
  register long x2 __asm__("x2") = arg2;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2) : "memory", "cc");
  return x0;
}

__attribute__((no_stack_protector)) inline long syscall_1(long nr, long arg0) {
  register long x8 __asm__("x8") = nr;
  register long x0 __asm__("x0") = arg0;
  __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory", "cc");
  return x0;
}

__attribute__((no_stack_protector)) inline int sys_open(const char *pathname, int flags) {
  return syscall_3(SYS_openat, -100 /* AT_FDCWD */, (long)pathname, flags);
}

__attribute__((no_stack_protector)) inline ssize_t sys_read(int fd, void *buf, size_t count) {
  return syscall_3(SYS_read, fd, (long)buf, count);
}

__attribute__((no_stack_protector)) inline int sys_close(int fd) {
  return syscall_1(SYS_close, fd);
}

// ===================================================================
// Page Size Detection via Auxiliary Vector
// ===================================================================
// This function reads the page size from the kernel's auxiliary vector
// by directly parsing /proc/self/auxv. We CANNOT use getauxval(AT_PAGESZ)
// because:
//   1. getauxval() is part of glibc which isn't initialized yet
//   2. We're running in the IFUNC resolver context before libraries load
//
// The auxiliary vector is a set of key-value pairs passed by the kernel
// to each process at startup, containing information like page size,
// clock tick rate, etc. AT_PAGESZ (type 6) contains the system page size.
//
// Fallback: 16KB (common on newer ARM64 systems, safe for 4KB systems)
// ===================================================================
__attribute__((no_stack_protector)) inline size_t getPageSizeFromAuxv() {
  unsigned char buffer[512];
  int fd = sys_open("/proc/self/auxv", O_RDONLY);

  if (fd < 0) {
    return kPageSize16K;
  }

  ssize_t bytes_read = sys_read(fd, buffer, sizeof(buffer));
  sys_close(fd);

  if (bytes_read < (ssize_t)sizeof(Elf64_auxv_t)) {
    return kPageSize16K;
  }

  Elf64_auxv_t *auxv = (Elf64_auxv_t *)buffer;
  Elf64_auxv_t *auxv_end = (Elf64_auxv_t *)(buffer + bytes_read);

  while (auxv < auxv_end && auxv->a_type != AT_NULL) {
    if (auxv->a_type == AT_PAGESZ) {
      size_t pagesize = auxv->a_un.a_val;
      if (pagesize == kPageSize4K || pagesize == kPageSize16K) {
        return pagesize;
      }
      return kPageSize16K;
    }
    auxv++;
  }

  return kPageSize16K;
}

#endif  // __aarch64__

}  // namespace ifunc
}  // namespace mesh

#endif  // __linux__

#endif  // MESH__IFUNC_RESOLVER_H
