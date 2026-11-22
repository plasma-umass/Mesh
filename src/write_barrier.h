// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// WriteBarrier: blocks writes to a span while meshing copies its live objects
// elsewhere, then releases the writers once the span has been remapped.
//
// Protocol, per mesh pass:
//
//   passBegin(arena)               once, before any mesh in the pass
//   for each mesh:
//     arm(remove)                  writes to remove now block
//     ... copy remove -> keep ...
//     mmap(remove, MAP_FIXED, keep's file offset)
//     commit(remove)               blocked writers retry against new mapping
//   passEnd(arena)                 once, after the last mesh in the pass
//
// Modes (best available is chosen at init; MESH_BARRIER overrides):
//
//   UffdKernel  userfaultfd write-protect, kernel-mode faults included.
//               Writers block in the kernel -- including syscalls such as
//               read(2) into a span being meshed, which otherwise fail with
//               EFAULT.  Needs CAP_SYS_PTRACE, vm.unprivileged_userfaultfd=1,
//               or access to /dev/userfaultfd.
//   UffdUser    userfaultfd with UFFD_USER_MODE_ONLY.  User-mode writers block
//               in the kernel; kernel-mode writers see EFAULT, as with
//               mprotect (handled by the EFAULT-retry syscall wrappers).
//   Mprotect    mprotect(PROT_READ); writers take SIGSEGV and wait in Mesh's
//               signal handler.  Used on non-Linux and pre-5.19 kernels.
//
// The userfaultfd modes require UFFD_FEATURE_WP_HUGETLBFS_SHMEM (Linux 5.19)
// since the arena is a shmem (memfd) mapping.
//
// Note: mmap(MAP_FIXED) replaces the registered VMA, so a span remapped earlier
// in a pass is no longer registered.  arm() re-registers on demand.

#pragma once
#ifndef MESH_WRITE_BARRIER_H
#define MESH_WRITE_BARRIER_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"

#if defined(__linux__) && __has_include(<linux/userfaultfd.h>)
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#if defined(UFFD_FEATURE_WP_HUGETLBFS_SHMEM) && defined(UFFD_USER_MODE_ONLY) && defined(SYS_userfaultfd)
#define MESH_HAVE_UFFD_WP 1
#endif
#endif

namespace mesh {

enum class BarrierMode : int {
  Mprotect = 0,
  UffdUser = 1,
  UffdKernel = 2,
};

inline const char *barrierModeName(BarrierMode m) {
  switch (m) {
  case BarrierMode::UffdKernel:
    return "uffd";
  case BarrierMode::UffdUser:
    return "uffd-user";
  case BarrierMode::Mprotect:
  default:
    return "mprotect";
  }
}

class WriteBarrier {
private:
  DISALLOW_COPY_AND_ASSIGN(WriteBarrier);

public:
  WriteBarrier() = default;

  // Select and initialize the best available mode.  Honors MESH_BARRIER
  // ("uffd", "uffd-user", "mprotect"); a requested mode that is unavailable
  // falls back to the next weaker one.  Safe to call again (e.g. after fork).
  void init() {
    closeUffd();
    _mode = BarrierMode::Mprotect;
#ifdef MESH_HAVE_UFFD_WP
    BarrierMode want = BarrierMode::UffdKernel;
    if (const char *env = getenv("MESH_BARRIER")) {
      if (strcmp(env, "mprotect") == 0) {
        want = BarrierMode::Mprotect;
      } else if (strcmp(env, "uffd-user") == 0) {
        want = BarrierMode::UffdUser;
      }
    }
    if (want == BarrierMode::UffdKernel && openUffd(false)) {
      _mode = BarrierMode::UffdKernel;
    } else if (want != BarrierMode::Mprotect && openUffd(true)) {
      _mode = BarrierMode::UffdUser;
    }
#endif
  }

  // In the child after fork(2): the inherited uffd refers to the *parent's*
  // mm, so it must never be used here.  Registrations are not inherited
  // (no UFFD_FEATURE_EVENT_FORK), so a fresh descriptor is all we need.
  void afterForkChild() {
    init();
  }

  inline BarrierMode mode() const {
    return _mode;
  }

  // True if kernel-mode writes (syscalls) to an armed span block rather than
  // failing with EFAULT.
  inline bool blocksKernelWrites() const {
    return _mode == BarrierMode::UffdKernel;
  }

  void passBegin(void *arena, size_t len) {
#ifdef MESH_HAVE_UFFD_WP
    if (isUffd()) {
      // Best effort: if this fails, arm() registers each span on demand.
      _passRegistered = uffdRegister(arena, len);
    }
#endif
    (void)arena;
    (void)len;
  }

  void passEnd(void *arena, size_t len) {
#ifdef MESH_HAVE_UFFD_WP
    if (isUffd()) {
      // Drop registrations so normal execution runs on plain VMAs (uffd-wp
      // VMAs disable fault-around on shmem).  Also clears any stale WP bits.
      uffdio_range r{reinterpret_cast<uintptr_t>(arena), len};
      if (ioctl(_uffd, UFFDIO_UNREGISTER, &r) != 0) {
        debug("mesh: UFFDIO_UNREGISTER failed: %d (%s)\n", errno, strerror(errno));
      }
      _passRegistered = false;
    }
#endif
    (void)arena;
    (void)len;
  }

  // Block writes to [ptr, ptr+len).
  void arm(void *ptr, size_t len) {
#ifdef MESH_HAVE_UFFD_WP
    if (isUffd()) {
      if (_passRegistered && uffdWriteProtect(ptr, len, true)) {
        return;
      }
      // Either whole-arena registration failed, or the range was remapped
      // earlier in this pass and its new VMA is unregistered.
      if (uffdRegister(ptr, len) && uffdWriteProtect(ptr, len, true)) {
        return;
      }
      debug("mesh: uffd write-protect failed (%d: %s); using mprotect for %p\n", errno, strerror(errno), ptr);
      // fall through: the SIGSEGV handler remains installed in every mode.
    }
#endif
    int r = mprotect(ptr, len, PROT_READ);
    hard_assert(r == 0);
  }

  // [ptr, ptr+len) has been remapped read/write onto its new physical
  // pages; wake any writers blocked on the old mapping so they retry.
  void commit(void *ptr, size_t len) {
#ifdef MESH_HAVE_UFFD_WP
    if (isUffd()) {
      // Wake is keyed on (ctx, address), not on the VMA, so it reaches
      // writers that faulted on the VMA mmap(MAP_FIXED) just replaced.
      // Unread fault events are discarded by the wake.
      uffdio_range r{reinterpret_cast<uintptr_t>(ptr), len};
      int err = ioctl(_uffd, UFFDIO_WAKE, &r);
      hard_assert_msg(err == 0, "UFFDIO_WAKE failed: %d", errno);
    }
#endif
    // Mprotect: the remap restored PROT_WRITE; signal-handler waiters
    // proceed once the mesh releases its size-class lock.
    (void)ptr;
    (void)len;
  }

private:
  inline bool isUffd() const {
    return _mode != BarrierMode::Mprotect;
  }

  void closeUffd() {
    if (_uffd >= 0) {
      close(_uffd);
      _uffd = -1;
    }
    _passRegistered = false;
  }

#ifdef MESH_HAVE_UFFD_WP
  bool openUffd(bool userModeOnly) {
    const int flags = O_CLOEXEC | O_NONBLOCK | (userModeOnly ? UFFD_USER_MODE_ONLY : 0);
    int fd = static_cast<int>(syscall(SYS_userfaultfd, flags));
#ifdef USERFAULTFD_IOC_NEW
    if (fd < 0 && errno == EPERM) {
      // Linux 6.1+: access to /dev/userfaultfd grants kernel-mode faults
      // without CAP_SYS_PTRACE or the sysctl.
      int dev = open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
      if (dev >= 0) {
        fd = ioctl(dev, USERFAULTFD_IOC_NEW, flags);
        close(dev);
      }
    }
#endif
    if (fd < 0) {
      return false;
    }

    uffdio_api api{};
    api.api = UFFD_API;
    api.features = UFFD_FEATURE_WP_HUGETLBFS_SHMEM;
    if (ioctl(fd, UFFDIO_API, &api) != 0 || !(api.features & UFFD_FEATURE_WP_HUGETLBFS_SHMEM) ||
        !(api.ioctls & (1ULL << _UFFDIO_REGISTER))) {
      close(fd);
      return false;
    }

    _uffd = fd;
    return true;
  }

  bool uffdRegister(void *ptr, size_t len) {
    uffdio_register r{};
    r.range.start = reinterpret_cast<uintptr_t>(ptr);
    r.range.len = len;
    r.mode = UFFDIO_REGISTER_MODE_WP;
    return ioctl(_uffd, UFFDIO_REGISTER, &r) == 0 && (r.ioctls & (1ULL << _UFFDIO_WRITEPROTECT));
  }

  bool uffdWriteProtect(void *ptr, size_t len, bool wp) {
    uffdio_writeprotect w{};
    w.range.start = reinterpret_cast<uintptr_t>(ptr);
    w.range.len = len;
    w.mode = wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0;
    return ioctl(_uffd, UFFDIO_WRITEPROTECT, &w) == 0;
  }
#endif

  BarrierMode _mode{BarrierMode::Mprotect};
  int _uffd{-1};
  bool _passRegistered{false};
};

}  // namespace mesh

#endif  // MESH_WRITE_BARRIER_H
