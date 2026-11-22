// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// Standalone (Mesh-independent) probe validating the userfaultfd write-protect
// protocol Mesh's UffdBarrier relies on, on a memfd (shmem) MAP_SHARED arena:
//
//   1. register(remove, WP) + writeprotect(remove)
//   2. user-mode writers and kernel-mode writers (read(2) into the page) block
//   3. copy remove -> keep, then mmap(MAP_FIXED) remove onto keep's file offset
//      (this *replaces* the registered VMA while writers are asleep)
//   4. UFFDIO_WAKE(remove) -- without ever reading the fault events
//   5. writers retry against the new mapping; their writes land in keep's page
//
// plus the abort path: writeprotect(remove, 0) wakes writers onto the original
// page with no remap.
//
// Usage: uffd-barrier-probe [--user-mode-only]
// Kernel-mode faults require CAP_SYS_PTRACE, vm.unprivileged_userfaultfd=1,
// or read/write access to /dev/userfaultfd.

#include <errno.h>
#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {

size_t kPage = 0;

#define CHECK(cond, ...)                                            \
  do {                                                              \
    if (!(cond)) {                                                  \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);          \
      fprintf(stderr, __VA_ARGS__);                                 \
      fprintf(stderr, " (errno %d: %s)\n", errno, strerror(errno)); \
      exit(1);                                                      \
    }                                                               \
  } while (0)

int openUffd(bool userModeOnly) {
  int flags = O_CLOEXEC | O_NONBLOCK | (userModeOnly ? UFFD_USER_MODE_ONLY : 0);
  int fd = static_cast<int>(syscall(SYS_userfaultfd, flags));
  if (fd < 0 && errno == EPERM) {
    int dev = open("/dev/userfaultfd", O_RDWR | O_CLOEXEC);
    if (dev >= 0) {
      fd = ioctl(dev, USERFAULTFD_IOC_NEW, flags);
      close(dev);
    }
  }
  CHECK(fd >= 0, "userfaultfd(%s)", userModeOnly ? "UFFD_USER_MODE_ONLY" : "kernel-mode");

  uffdio_api api{};
  api.api = UFFD_API;
  api.features = UFFD_FEATURE_WP_HUGETLBFS_SHMEM;
  CHECK(ioctl(fd, UFFDIO_API, &api) == 0, "UFFDIO_API");
  CHECK(api.features & UFFD_FEATURE_WP_HUGETLBFS_SHMEM, "no WP_HUGETLBFS_SHMEM");
  CHECK(api.ioctls & (1ULL << _UFFDIO_REGISTER), "no UFFDIO_REGISTER");
  return fd;
}

void uffdRegister(int uffd, void *p, size_t len) {
  uffdio_register r{};
  r.range.start = reinterpret_cast<uintptr_t>(p);
  r.range.len = len;
  r.mode = UFFDIO_REGISTER_MODE_WP;
  CHECK(ioctl(uffd, UFFDIO_REGISTER, &r) == 0, "UFFDIO_REGISTER");
  CHECK(r.ioctls & (1ULL << _UFFDIO_WRITEPROTECT), "WRITEPROTECT not allowed on range");
}

void uffdUnregister(int uffd, void *p, size_t len) {
  uffdio_range r{reinterpret_cast<uintptr_t>(p), len};
  CHECK(ioctl(uffd, UFFDIO_UNREGISTER, &r) == 0, "UFFDIO_UNREGISTER");
}

void uffdProtect(int uffd, void *p, size_t len, bool wp) {
  uffdio_writeprotect w{};
  w.range.start = reinterpret_cast<uintptr_t>(p);
  w.range.len = len;
  w.mode = wp ? UFFDIO_WRITEPROTECT_MODE_WP : 0;
  CHECK(ioctl(uffd, UFFDIO_WRITEPROTECT, &w) == 0, "UFFDIO_WRITEPROTECT(%d)", wp);
}

void uffdWake(int uffd, void *p, size_t len) {
  uffdio_range r{reinterpret_cast<uintptr_t>(p), len};
  CHECK(ioctl(uffd, UFFDIO_WAKE, &r) == 0, "UFFDIO_WAKE");
}

int pendingEvents(int uffd) {
  int n = 0;
  uffd_msg msg;
  while (read(uffd, &msg, sizeof msg) == sizeof msg) {
    if (msg.event == UFFD_EVENT_PAGEFAULT && (msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP))
      n++;
  }
  return n;
}

struct Arena {
  int memfd;
  char *base;
  size_t pages;
  char *page(size_t i) const {
    return base + i * kPage;
  }
};

Arena makeArena(size_t pages) {
  Arena a{};
  a.pages = pages;
  a.memfd = static_cast<int>(syscall(SYS_memfd_create, "uffd-probe", MFD_CLOEXEC));
  CHECK(a.memfd >= 0, "memfd_create");
  CHECK(ftruncate(a.memfd, pages * kPage) == 0, "ftruncate");
  void *p = mmap(nullptr, pages * kPage, PROT_READ | PROT_WRITE, MAP_SHARED, a.memfd, 0);
  CHECK(p != MAP_FAILED, "mmap arena");
  a.base = static_cast<char *>(p);
  return a;
}

void destroyArena(Arena &a) {
  munmap(a.base, a.pages * kPage);
  close(a.memfd);
}

constexpr size_t kKeep = 0, kRemove = 1;
constexpr size_t kUserOff = 64, kKernOff = 512;
constexpr size_t kKernLen = 128;
constexpr int kUserWriters = 4;

// Returns true if kernel-mode writes blocked and later succeeded; false if
// they returned EFAULT (expected under UFFD_USER_MODE_ONLY).
bool commitPath(bool userModeOnly) {
  Arena a = makeArena(4);
  int uffd = openUffd(userModeOnly);

  // keep: live object at offset 0; remove: live objects at kUserOff.. and kKernOff..
  memset(a.page(kKeep), 0, kPage);
  memset(a.page(kRemove), 0, kPage);
  a.page(kKeep)[0] = 'K';
  for (int i = 0; i < kUserWriters; i++)
    a.page(kRemove)[kUserOff + i] = 'r';

  // registration on a whole-arena basis, as UffdBarrier::passBegin() does
  uffdRegister(uffd, a.base, a.pages * kPage);
  uffdProtect(uffd, a.page(kRemove), kPage, true);

  std::atomic<int> userDone{0};
  std::thread users[kUserWriters];
  for (int i = 0; i < kUserWriters; i++) {
    users[i] = std::thread([&, i] {
      volatile char *p = a.page(kRemove) + kUserOff + i;
      *p = static_cast<char>('A' + i);  // blocks under WP
      userDone.fetch_add(1);
    });
  }

  int pipefd[2];
  CHECK(pipe(pipefd) == 0, "pipe");
  char payload[kKernLen];
  for (size_t i = 0; i < kKernLen; i++)
    payload[i] = static_cast<char>('a' + i % 26);
  CHECK(write(pipefd[1], payload, kKernLen) == (ssize_t)kKernLen, "pipe write");

  std::atomic<ssize_t> kernResult{-2};
  std::atomic<int> kernErrno{0};
  std::thread kern([&] {
    ssize_t n = read(pipefd[0], a.page(kRemove) + kKernOff, kKernLen);  // copy_to_user faults
    kernErrno = errno;
    kernResult = n;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  CHECK(userDone.load() == 0, "user-mode writer was not blocked by WP");
  bool kernBlocked = kernResult.load() == -2;
  if (!userModeOnly) {
    CHECK(kernBlocked, "kernel-mode write not blocked (result %zd errno %d)", kernResult.load(), kernErrno.load());
  }

  int events = pendingEvents(uffd);  // informational; wake must work even if unread
  (void)events;

  // "consume": copy remove's live objects into keep (disjoint offsets)
  memcpy(a.page(kKeep) + kUserOff, a.page(kRemove) + kUserOff, kUserWriters);
  memcpy(a.page(kKeep) + kKernOff, a.page(kRemove) + kKernOff, kKernLen);

  // finalize: replace the registered VMA under sleeping writers
  void *r = mmap(a.page(kRemove), kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, a.memfd, kKeep * kPage);
  CHECK(r == a.page(kRemove), "remap");

  // release the old physical page as Mesh does
  CHECK(fallocate(a.memfd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, kRemove * kPage, kPage) == 0, "punch");

  uffdWake(uffd, a.page(kRemove), kPage);

  for (auto &t : users)
    t.join();
  kern.join();

  // both virtual pages now alias keep's physical page
  CHECK(a.page(kRemove)[0] == 'K', "remove not aliased to keep");
  for (int i = 0; i < kUserWriters; i++) {
    CHECK(a.page(kKeep)[kUserOff + i] == 'A' + i, "user write %d lost (got %c)", i, a.page(kKeep)[kUserOff + i]);
  }

  bool kernOk;
  if (kernResult.load() == (ssize_t)kKernLen) {
    CHECK(memcmp(a.page(kKeep) + kKernOff, payload, kKernLen) == 0, "kernel-mode write lost");
    kernOk = true;
  } else {
    CHECK(userModeOnly && kernResult.load() == -1 && kernErrno.load() == EFAULT, "unexpected read() result %zd/%d",
          kernResult.load(), kernErrno.load());
    kernOk = false;
  }

  // the remapped VMA must be unregistered: a fresh WP attempt on it should fail
  uffdio_writeprotect w{};
  w.range.start = reinterpret_cast<uintptr_t>(a.page(kRemove));
  w.range.len = kPage;
  w.mode = UFFDIO_WRITEPROTECT_MODE_WP;
  bool stillRegistered = ioctl(uffd, UFFDIO_WRITEPROTECT, &w) == 0;
  CHECK(!stillRegistered, "replaced VMA unexpectedly still registered");

  uffdUnregister(uffd, a.base, a.pages * kPage);  // must tolerate the hole
  printf("commit path OK (%s): %d wp events pending before wake, kernel-mode write %s\n",
         userModeOnly ? "user-mode-only" : "kernel-mode", events, kernOk ? "blocked+succeeded" : "EFAULT");

  close(pipefd[0]);
  close(pipefd[1]);
  close(uffd);
  destroyArena(a);
  return kernOk;
}

void abortPath(bool userModeOnly) {
  Arena a = makeArena(2);
  int uffd = openUffd(userModeOnly);
  memset(a.page(kRemove), 'r', kPage);

  uffdRegister(uffd, a.base, a.pages * kPage);
  uffdProtect(uffd, a.page(kRemove), kPage, true);

  std::atomic<bool> done{false};
  std::thread writer([&] {
    a.page(kRemove)[kUserOff] = 'W';
    done = true;
  });

  // the mesher would poll the uffd during consume() and notice the stall
  pollfd pfd{uffd, POLLIN, 0};
  CHECK(poll(&pfd, 1, 2000) == 1, "no fault event within 2s");
  CHECK(pendingEvents(uffd) == 1, "expected exactly one wp event");
  CHECK(!done.load(), "writer not blocked");

  // abort: drop WP (wakes) and leave the mapping untouched
  uffdProtect(uffd, a.page(kRemove), kPage, false);
  writer.join();
  CHECK(a.page(kRemove)[kUserOff] == 'W', "aborted write lost");

  uffdUnregister(uffd, a.base, a.pages * kPage);
  printf("abort path OK (%s)\n", userModeOnly ? "user-mode-only" : "kernel-mode");
  close(uffd);
  destroyArena(a);
}

// Many short mesh cycles with continuous writers, to shake out races between
// remap, wake, and fault retry.
void stress(bool userModeOnly, int rounds) {
  constexpr size_t kPairs = 32;
  constexpr size_t kSlots = 64;  // per page; keep uses even slots, remove odd
  Arena a = makeArena(kPairs * 2);
  int uffd = openUffd(userModeOnly);
  const size_t slot = kPage / kSlots;

  for (int round = 0; round < rounds; round++) {
    // reset: identity mapping, fresh contents
    void *r = mmap(a.base, a.pages * kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, a.memfd, 0);
    CHECK(r == a.base, "reset mmap");
    memset(a.base, 0, a.pages * kPage);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> counters[kPairs] = {};
    std::thread writers[4];
    for (int t = 0; t < 4; t++) {
      writers[t] = std::thread([&, t] {
        uint64_t n = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          for (size_t p = t; p < kPairs; p += 4) {
            // odd slot in remove page p; value is a monotonically increasing counter
            volatile uint64_t *obj = reinterpret_cast<uint64_t *>(a.page(2 * p + 1) + slot * (1 + 2 * t));
            *obj = ++n;
            counters[p].store(n, std::memory_order_release);
          }
        }
      });
    }

    uffdRegister(uffd, a.base, a.pages * kPage);
    for (size_t p = 0; p < kPairs; p++) {
      char *keep = a.page(2 * p), *remove = a.page(2 * p + 1);
      uffdProtect(uffd, remove, kPage, true);
      for (size_t s = 1; s < kSlots; s += 2)
        memcpy(keep + s * slot, remove + s * slot, slot);
      CHECK(mmap(remove, kPage, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, a.memfd, (2 * p) * kPage) == remove,
            "remap");
      uffdWake(uffd, remove, kPage);
    }
    pendingEvents(uffd);
    uffdUnregister(uffd, a.base, a.pages * kPage);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    stop = true;
    for (auto &t : writers)
      t.join();

    // last value written by each writer must be visible through the alias
    for (size_t p = 0; p < kPairs; p++) {
      int t = static_cast<int>(p % 4);
      uint64_t got = *reinterpret_cast<uint64_t *>(a.page(2 * p) + slot * (1 + 2 * t));
      uint64_t want = counters[p].load(std::memory_order_acquire);
      CHECK(got == want, "round %d pair %zu: lost write (got %lu want %lu)", round, p, (unsigned long)got,
            (unsigned long)want);
    }
  }
  printf("stress OK (%s): %d rounds x %zu meshes\n", userModeOnly ? "user-mode-only" : "kernel-mode", rounds, kPairs);
  close(uffd);
  destroyArena(a);
}

}  // namespace

int main(int argc, char **argv) {
  kPage = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  bool userModeOnly = argc > 1 && strcmp(argv[1], "--user-mode-only") == 0;
  commitPath(userModeOnly);
  abortPath(userModeOnly);
  stress(userModeOnly, 200);
  printf("PASS\n");
  return 0;
}
