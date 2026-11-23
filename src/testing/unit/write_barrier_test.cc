// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// Tests for the mesh write barrier (write_barrier.h): writers racing a mesh,
// from user mode and from the kernel (read(2) into an object being meshed),
// must not lose data.  Under the userfaultfd barrier, kernel-mode writes block
// until the mesh completes instead of failing with EFAULT.
//
// Run with MESH_BARRIER=mprotect|uffd-user|uffd to select a mode; kernel-mode
// uffd needs root, vm.unprivileged_userfaultfd=1, or /dev/userfaultfd access.

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#ifdef __linux__
#include <linux/magic.h>
#include <sys/vfs.h>
#endif
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <thread>

#include "gtest/gtest.h"

#include "internal.h"
#include "meshing.h"
#include "real.h"
#include "runtime.h"

using namespace mesh;

namespace {

constexpr uint32_t StrLen = 128;
constexpr size_t kUserLen = 8;
constexpr size_t kKernOff = 16;
constexpr size_t kKernLen = 64;

template <size_t PageSize>
GlobalHeap<PageSize> &heapFor() {
  return runtime<PageSize>().heap();
}

template <size_t PageSize>
struct MeshPair {
  MiniHeap<PageSize> *keep{nullptr};
  MiniHeap<PageSize> *remove{nullptr};
  char *keepObj{nullptr};
  char *removeObj{nullptr};
  char *removeAlias{nullptr};  // removeObj's offset, addressed via keep's span
};

// Two single-object miniheaps of the same size class at disjoint offsets.
template <size_t PageSize>
MeshPair<PageSize> makePair() {
  auto &gheap = heapFor<PageSize>();
  const uint32_t objCount = std::min(static_cast<uint32_t>(PageSize / StrLen), 1024U);
  const auto tid = gettid();
  FixedArray<MiniHeap<PageSize>, 1> array{};

  MeshPair<PageSize> p;
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  p.keep = array[0];
  array.clear();
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  p.remove = array[0];
  array.clear();

  p.keepObj = reinterpret_cast<char *>(p.keep->mallocAt(gheap.arenaBegin(), 0));
  p.removeObj = reinterpret_cast<char *>(p.remove->mallocAt(gheap.arenaBegin(), objCount - 1));
  p.removeAlias = p.keepObj + (objCount - 1) * StrLen;
  memset(p.keepObj, 'K', StrLen);
  memset(p.removeObj, 'R', StrLen);
  return p;
}

template <size_t PageSize>
void destroyPair(MeshPair<PageSize> &p) {
  auto &gheap = heapFor<PageSize>();
  gheap.free(p.keepObj);
  gheap.free(p.removeObj);
  gheap.freeMiniheap(p.keep);
  gheap.scavenge(true);
}

// Writers started before the mesh and released by the mesh hook, i.e. while
// the remove span is write-protected and before anything is copied.
struct Race {
  char *userTarget{nullptr};
  char *kernTarget{nullptr};
  int pipeRd{-1};

  std::atomic<bool> go{false};
  std::atomic<bool> userDone{false};
  std::atomic<ssize_t> kernResult{-2};
  std::atomic<int> kernErrno{0};

  bool userDoneDuringMesh{false};
  bool kernDoneDuringMesh{false};
  int hookCalls{0};

  static void hook(void *arg) {
    auto *r = static_cast<Race *>(arg);
    r->hookCalls++;
    r->go.store(true, std::memory_order_release);
    // long enough for both writers to fault on the protected span
    usleep(200 * 1000);
    r->userDoneDuringMesh = r->userDone.load();
    r->kernDoneDuringMesh = r->kernResult.load() != -2;
  }
};

char kernPayload(size_t i) {
  return static_cast<char>('a' + i % 26);
}

template <size_t PageSize>
void racingWritersImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &gheap = heapFor<PageSize>();
  gheap.setMeshPeriodMs(kZeroMs);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);

  const auto &barrier = gheap.writeBarrier();
  printf("write barrier mode: %s\n", barrierModeName(barrier.mode()));
  if (const char *env = getenv("MESH_BARRIER")) {
    if (strcmp(env, "mprotect") == 0) {
      ASSERT_EQ(barrier.mode(), BarrierMode::Mprotect);
    }
  }

  const auto statsBefore = barrier.stats();
  auto p = makePair<PageSize>();

  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0);
  char payload[kKernLen];
  for (size_t i = 0; i < kKernLen; i++)
    payload[i] = kernPayload(i);
  ASSERT_EQ(write(pipefd[1], payload, kKernLen), static_cast<ssize_t>(kKernLen));

  Race race;
  race.userTarget = p.removeObj;
  race.kernTarget = p.removeObj + kKernOff;
  race.pipeRd = pipefd[0];

  std::thread user([&] {
    while (!race.go.load(std::memory_order_acquire)) {
    }
    memset(race.userTarget, 'u', kUserLen);
    race.userDone = true;
  });
  std::thread kern([&] {
    while (!race.go.load(std::memory_order_acquire)) {
    }
    ssize_t n = read(race.pipeRd, race.kernTarget, kKernLen);  // copy_to_user into a protected span
    race.kernErrno = errno;
    race.kernResult = n;
  });

  gheap.setMeshHookForTesting(&Race::hook, &race);
  gheap.meshLocked(p.keep, p.remove);
  gheap.setMeshHookForTesting(nullptr, nullptr);
  user.join();
  kern.join();

  ASSERT_EQ(race.hookCalls, 1);
  EXPECT_FALSE(race.userDoneDuringMesh) << "user-mode write was not blocked by the barrier";

  // stats: the hook held the span armed for >= 200ms, and the user-mode
  // writer (plus the kernel-mode one, under uffd) stalled on it
  const auto statsAfter = barrier.stats();
  EXPECT_EQ(statsAfter.meshes - statsBefore.meshes, 1UL);
  EXPECT_GE(statsAfter.maxWindowNs, 200UL * 1000 * 1000);
  EXPECT_GE(statsAfter.stalls - statsBefore.stalls, barrier.blocksKernelWrites() ? 2UL : 1UL);

  // the mesh happened
  ASSERT_EQ(p.keep->meshCount(), 2ULL);
  EXPECT_EQ(p.keepObj[0], 'K');

  // user-mode write survived and is visible through both virtual addresses
  for (size_t i = 0; i < kUserLen; i++) {
    ASSERT_EQ(p.removeObj[i], 'u') << i;
    ASSERT_EQ(p.removeAlias[i], 'u') << i;
  }
  ASSERT_EQ(p.removeObj[kUserLen], 'R');

  if (barrier.blocksKernelWrites()) {
    EXPECT_FALSE(race.kernDoneDuringMesh) << "kernel-mode write was not blocked by the barrier";
    ASSERT_EQ(race.kernResult.load(), static_cast<ssize_t>(kKernLen));
    for (size_t i = 0; i < kKernLen; i++) {
      ASSERT_EQ(p.removeObj[kKernOff + i], kernPayload(i)) << i;
      ASSERT_EQ(p.removeAlias[kKernOff + i], kernPayload(i)) << i;
    }
  } else {
    // known limitation of the mprotect and user-mode-only barriers: the
    // syscall fails rather than waiting (only recv/recvmsg are retried).
    EXPECT_EQ(race.kernResult.load(), -1);
    EXPECT_EQ(race.kernErrno.load(), EFAULT);
  }

  close(pipefd[0]);
  close(pipefd[1]);
  destroyPair(p);
  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
}

// Mesh once; returns false on any failure (usable without gtest, e.g. in a
// forked child).
template <size_t PageSize>
bool meshOnce() {
  auto &gheap = heapFor<PageSize>();
  if (gheap.getAllocatedMiniheapCount() != 0)
    return false;
  auto p = makePair<PageSize>();
  gheap.meshLocked(p.keep, p.remove);
  bool ok = p.keep->meshCount() == 2 && p.removeObj[0] == 'R' && p.removeAlias[0] == 'R';
  p.removeObj[1] = 'x';
  ok = ok && p.removeAlias[1] == 'x';
  destroyPair(p);
  return ok && gheap.getAllocatedMiniheapCount() == 0;
}

template <size_t PageSize>
void meshAfterForkImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &gheap = heapFor<PageSize>();
  gheap.setMeshPeriodMs(kZeroMs);
  const auto parentMode = gheap.writeBarrier().mode();

  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    // the child must have its own userfaultfd (same mode as the parent)
    bool ok = gheap.writeBarrier().mode() == parentMode && meshOnce<PageSize>();
    _exit(ok ? 0 : 1);
  }
  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status)) << "child terminated abnormally";
  ASSERT_EQ(WEXITSTATUS(status), 0) << "mesh failed in forked child";

  // and the parent's barrier still works
  ASSERT_TRUE(meshOnce<PageSize>());
}

template <size_t PageSize>
void mallctlImpl() {
  auto &gheap = heapFor<PageSize>();
  size_t mode = 99;
  size_t len = sizeof(mode);
  ASSERT_EQ(gheap.mallctl("mesh.barrier", &mode, &len, nullptr, 0), 0);
  EXPECT_EQ(mode, static_cast<size_t>(gheap.writeBarrier().mode()));
}

int gTestSignals = 0;
void testSegvHandler(int) {
  gTestSignals++;
}

template <size_t PageSize>
void signalOwnershipImpl() {
  auto &rt = runtime<PageSize>();
  const auto &barrier = rt.heap().writeBarrier();
  // as libmesh_init does (the unit tests don't link libmesh.cc, so we call
  // Runtime's sigaction interposer directly below)
  rt.installSegfaultHandler();
  EXPECT_EQ(rt.segfaultHandlerInstalled(), barrier.needsSignalHandler());
  if (kMeshingEnabled && barrier.mode() == BarrierMode::Mprotect) {
    EXPECT_TRUE(rt.segfaultHandlerInstalled());
  }

  struct sigaction mine{};
  mine.sa_handler = testSegvHandler;
  struct sigaction saved{};
  ASSERT_EQ(rt.sigaction(SIGSEGV, &mine, &saved), 0);

  // a query (act == nullptr) must report, not clear, the installed handler
  struct sigaction q1{}, q2{};
  ASSERT_EQ(rt.sigaction(SIGSEGV, nullptr, &q1), 0);
  ASSERT_EQ(rt.sigaction(SIGSEGV, nullptr, &q2), 0);
  EXPECT_EQ(q1.sa_handler, testSegvHandler);
  EXPECT_EQ(q2.sa_handler, testSegvHandler);

  // what the kernel actually has installed
  struct sigaction kernel{};
  ASSERT_EQ(mesh::real::sigaction(SIGSEGV, nullptr, &kernel), 0);
  if (rt.segfaultHandlerInstalled()) {
    EXPECT_NE(kernel.sa_handler, testSegvHandler) << "Mesh's handler should stay in front";
  } else {
    EXPECT_EQ(kernel.sa_handler, testSegvHandler) << "without Mesh's handler the app should own SIGSEGV";
  }

  ASSERT_EQ(rt.sigaction(SIGSEGV, &saved, nullptr), 0);
}

// A userfaultfd barrier that cannot write-protect a range must install the
// SIGSEGV handler and fall back to mprotect.  Runs in a child, since the
// fallback is permanent for the process.
template <size_t PageSize>
void fallbackImpl() {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  auto &rt = runtime<PageSize>();
  if (rt.heap().writeBarrier().mode() == BarrierMode::Mprotect) {
    GTEST_SKIP() << "userfaultfd barrier not active";
  }

  // userfaultfd-wp does not support mappings of regular (non-shmem) files,
  // so registering one fails.  Find a writable directory not on tmpfs.
  int fd = -1;
#ifdef __linux__
  const char *dirs[] = {getenv("TEST_TMPDIR"), "/var/tmp", "."};
  for (const char *dir : dirs) {
    struct statfs sfs;
    if (dir == nullptr || statfs(dir, &sfs) != 0 || sfs.f_type == TMPFS_MAGIC)
      continue;
    char path[512];
    snprintf(path, sizeof(path), "%s/mesh-wb-XXXXXX", dir);
    fd = mkstemp(path);
    if (fd >= 0) {
      unlink(path);
      break;
    }
  }
#endif
  if (fd < 0) {
    GTEST_SKIP() << "no writable non-tmpfs directory";
  }
  ASSERT_EQ(ftruncate(fd, PageSize), 0);

  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    void *ptr = mmap(nullptr, PageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ptr == MAP_FAILED)
      _exit(12);

    if (rt.segfaultHandlerInstalled() || rt.heap().writeBarrier().needsSignalHandler())
      _exit(13);
    rt.heap().beginMesh(nullptr, ptr, PageSize);  // -> WriteBarrier::arm
    if (!rt.heap().writeBarrier().needsSignalHandler())
      _exit(14);
    if (!rt.segfaultHandlerInstalled())
      _exit(15);
    // the range really is read-only now
    if (mprotect(ptr, PageSize, PROT_READ | PROT_WRITE) != 0)
      _exit(16);
    _exit(0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status));
  ASSERT_EQ(WEXITSTATUS(status), 0);
  close(fd);
  // the parent is unaffected
  EXPECT_FALSE(rt.segfaultHandlerInstalled());
}

}  // namespace

TEST(WriteBarrierTest, SignalHandlerOwnership) {
  if (getPageSize() == 4096) {
    signalOwnershipImpl<4096>();
  } else {
    signalOwnershipImpl<16384>();
  }
}

TEST(WriteBarrierTest, FallbackToMprotectInstallsHandler) {
  if (getPageSize() == 4096) {
    fallbackImpl<4096>();
  } else {
    fallbackImpl<16384>();
  }
}

TEST(WriteBarrierTest, RacingWritersDuringMesh) {
  if (getPageSize() == 4096) {
    racingWritersImpl<4096>();
  } else {
    racingWritersImpl<16384>();
  }
}

TEST(WriteBarrierTest, MeshAfterFork) {
  if (getPageSize() == 4096) {
    meshAfterForkImpl<4096>();
  } else {
    meshAfterForkImpl<16384>();
  }
}

TEST(WriteBarrierTest, MallctlReportsMode) {
  if (getPageSize() == 4096) {
    mallctlImpl<4096>();
  } else {
    mallctlImpl<16384>();
  }
}
