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
// (passBegin/passEnd are currently no-ops; they bracket the pass for
// barriers that need per-pass setup.)
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
// Userfaultfd registration is per span: arm() registers exactly the span it
// protects, and the remap in finalizeMesh replaces that VMA, which drops the
// registration again.  So nothing stays registered between meshes and there
// is no per-pass cost.  (Registering the whole arena once per pass makes each
// mesh ~1us cheaper, but UFFDIO_(UN)REGISTER is O(#VMAs) -- ~10ms each at 20k
// meshed VMAs, which meshing itself creates -- so it loses for any pass under
// ~25k meshes; kMaxMeshesPerIteration is 2500.)
//
// Only Mprotect needs Mesh's SIGSEGV handler.  In the userfaultfd modes the
// handler is not installed at all (applications get SIGSEGV/SIGBUS to
// themselves); if a userfaultfd operation ever fails, arm() invokes the
// fallback hook to install it before falling back to mprotect.
//
// Statistics: a stall is a write that hit an armed span (one per fault event
// under userfaultfd, one per SIGSEGV under mprotect).  The armed window is
// the time from arming a mesh's first span to committing it, an upper bound
// on how long any writer waited.

#pragma once
#ifndef MESH_WRITE_BARRIER_H
#define MESH_WRITE_BARRIER_H

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <atomic>

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
    _fellBack.store(false, std::memory_order_release);
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

  // True if writers to an armed span are delivered SIGSEGV (Mprotect mode, or
  // a userfaultfd mode that has fallen back to mprotect).
  inline bool needsSignalHandler() const {
    return _mode == BarrierMode::Mprotect || _fellBack.load(std::memory_order_acquire);
  }

  // Called (under the mesh locks) the first time a userfaultfd-mode barrier
  // falls back to mprotect; must install the SIGSEGV handler.
  using FallbackHook = void (*)();
  void setFallbackHook(FallbackHook hook) {
    _fallbackHook = hook;
  }

  struct Stats {
    size_t meshes;       // committed spans
    size_t stalls;       // writes that hit an armed span
    size_t maxWindowNs;  // longest arm -> commit window
    size_t totalWindowNs;
  };
  Stats stats() const {
    return Stats{_commits.load(std::memory_order_relaxed), _stalls.load(std::memory_order_relaxed),
                 _maxWindowNs.load(std::memory_order_relaxed), _totalWindowNs.load(std::memory_order_relaxed)};
  }

  // Mprotect: the SIGSEGV handler reports each meshing fault here.
  inline void noteSignalStall() {
    _stalls.fetch_add(1, std::memory_order_relaxed);
  }

  // True if kernel-mode writes (syscalls) to an armed span block rather than
  // failing with EFAULT.
  inline bool blocksKernelWrites() const {
    return _mode == BarrierMode::UffdKernel;
  }

  void passBegin(void *arena, size_t len) {
    (void)arena;
    (void)len;
  }

  void passEnd(void *arena, size_t len) {
    (void)arena;
    (void)len;
  }

  // Block writes to [ptr, ptr+len).
  void arm(void *ptr, size_t len) {
    if (_armedAtNs == 0) {
      _armedAtNs = nowNs();
    }
#ifdef MESH_HAVE_UFFD_WP
    if (isUffd()) {
      if (uffdRegister(ptr, len) && uffdWriteProtect(ptr, len, true)) {
        return;
      }
      debug("mesh: uffd write-protect failed (%d: %s); using mprotect for %p\n", errno, strerror(errno), ptr);
      if (!_fellBack.exchange(true, std::memory_order_acq_rel) && _fallbackHook != nullptr) {
        _fallbackHook();
      }
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
      // Count stalled writers; the wake below would discard their events.
      drainEvents();
      // Wake is keyed on (ctx, address), not on the VMA, so it reaches
      // writers that faulted on the VMA mmap(MAP_FIXED) just replaced.
      uffdio_range r{reinterpret_cast<uintptr_t>(ptr), len};
      int err = ioctl(_uffd, UFFDIO_WAKE, &r);
      hard_assert_msg(err == 0, "UFFDIO_WAKE failed: %d", errno);
    }
#endif
    // Mprotect: the remap restored PROT_WRITE; signal-handler waiters
    // proceed once the mesh releases its size-class lock.
    (void)ptr;
    (void)len;

    _commits.fetch_add(1, std::memory_order_relaxed);
    if (_armedAtNs != 0) {
      const size_t window = nowNs() - _armedAtNs;
      _armedAtNs = 0;
      _totalWindowNs.fetch_add(window, std::memory_order_relaxed);
      if (window > _maxWindowNs.load(std::memory_order_relaxed)) {
        _maxWindowNs.store(window, std::memory_order_relaxed);
      }
    }
  }

private:
  static size_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<size_t>(ts.tv_sec) * 1000000000ULL + static_cast<size_t>(ts.tv_nsec);
  }

  inline bool isUffd() const {
    return _mode != BarrierMode::Mprotect;
  }

  void closeUffd() {
    if (_uffd >= 0) {
      close(_uffd);
      _uffd = -1;
    }
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

  void drainEvents() {
    uffd_msg msgs[16];
    ssize_t n;
    while ((n = read(_uffd, msgs, sizeof(msgs))) > 0) {
      for (ssize_t i = 0; i < n / static_cast<ssize_t>(sizeof(uffd_msg)); i++) {
        if (msgs[i].event == UFFD_EVENT_PAGEFAULT) {
          _stalls.fetch_add(1, std::memory_order_relaxed);
        }
      }
    }
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
  std::atomic<bool> _fellBack{false};
  FallbackHook _fallbackHook{nullptr};

  size_t _armedAtNs{0};  // only touched by the (single, locked) mesher
  std::atomic<size_t> _commits{0};
  std::atomic<size_t> _stalls{0};
  std::atomic<size_t> _maxWindowNs{0};
  std::atomic<size_t> _totalWindowNs{0};
};

}  // namespace mesh

#endif  // MESH_WRITE_BARRIER_H
