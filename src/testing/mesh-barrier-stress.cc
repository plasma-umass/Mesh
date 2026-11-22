// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2026 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.
//
// End-to-end stress for Mesh's write barrier.  Run under libmesh:
//
//   LD_PRELOAD=libmesh.so MESH_BARRIER=uffd ./mesh-barrier-stress
//
// Fragments the heap (keeps ~1/8 of small objects, randomly placed), then
// runs full mesh passes (mesh.compact) back to back while:
//   - user-mode writer threads continuously rewrite the surviving objects
//   - kernel-mode writer threads read(2) from pipes into surviving objects
//   - a churn thread mallocs/frees so meshing stays enabled
// and finally checks that no write was lost.  Under MESH_BARRIER=uffd, no
// read(2) may fail with EFAULT; under the other barriers EFAULTs are counted.

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace {

// resolved at runtime so this binary needs only LD_PRELOAD=libmesh.so
using MallctlFn = int (*)(const char *, void *, size_t *, void *, size_t);
MallctlFn mesh_mallctl = nullptr;

constexpr size_t kObjSize = 256;
constexpr size_t kAllocated = 1 << 20;  // 256 MiB of 256-byte objects
constexpr size_t kKeepEvery = 8;
constexpr int kUserWriters = 4;
constexpr int kKernWriters = 2;
constexpr size_t kKernLen = 64;

struct Obj {
  uint64_t owner;
  uint64_t seq;         // written by user-mode writer `owner`
  char kern[kKernLen];  // written by read(2) in a kernel-mode writer
  uint64_t kernSeq;     // seq of the last successful read(2) into kern
  char pad[kObjSize - 3 * sizeof(uint64_t) - kKernLen];
};
static_assert(sizeof(Obj) == kObjSize, "object size");

size_t mallctlSize(const char *name) {
  size_t v = 0, len = sizeof(v);
  if (mesh_mallctl(name, &v, &len, nullptr, 0) != 0)
    return SIZE_MAX;
  return v;
}

size_t rssKiB() {
  FILE *f = fopen("/proc/self/statm", "r");
  size_t pages = 0, rss = 0;
  if (f) {
    if (fscanf(f, "%zu %zu", &pages, &rss) != 2)
      rss = 0;
    fclose(f);
  }
  return rss * (sysconf(_SC_PAGESIZE) / 1024);
}

void fillKern(char *buf, int writer, uint64_t seq) {
  for (size_t i = 0; i < kKernLen; i++)
    buf[i] = static_cast<char>((seq * 31 + writer * 7 + i) & 0x7f);
}

}  // namespace

int main(int argc, char **argv) {
  const int seconds = argc > 1 ? atoi(argv[1]) : 10;
  mesh_mallctl = reinterpret_cast<MallctlFn>(dlsym(RTLD_DEFAULT, "mesh_mallctl"));
  if (mesh_mallctl == nullptr) {
    fprintf(stderr, "mesh_mallctl not found: run with LD_PRELOAD=libmesh.so\n");
    return 2;
  }
  const size_t mode = mallctlSize("mesh.barrier");
  const char *modeName = mode == 2 ? "uffd" : mode == 1 ? "uffd-user" : mode == 0 ? "mprotect" : "unknown";
  printf("barrier: %s\n", modeName);

  std::vector<Obj *> all(kAllocated);
  for (auto &p : all) {
    p = static_cast<Obj *>(malloc(kObjSize));
    memset(p, 0, kObjSize);
  }

  // random survivors -> sparse, meshable spans
  std::mt19937_64 rng(42);
  std::vector<Obj *> live;
  for (auto &p : all) {
    if (rng() % kKeepEvery == 0) {
      live.push_back(p);
    } else {
      free(p);
    }
  }
  all.clear();
  all.shrink_to_fit();
  for (size_t i = 0; i < live.size(); i++) {
    live[i]->owner = i % kUserWriters;
  }
  const size_t rssBefore = rssKiB();
  printf("live objects: %zu, rss before meshing: %zu MiB\n", live.size(), rssBefore / 1024);

  std::atomic<bool> stop{false};
  std::vector<std::thread> threads;

  // user-mode writers: each owns live[i] with i % kUserWriters == w
  std::vector<uint64_t> finalSeq(kUserWriters, 0);
  for (int w = 0; w < kUserWriters; w++) {
    threads.emplace_back([&, w] {
      uint64_t seq = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        seq++;
        for (size_t i = w; i < live.size(); i += kUserWriters) {
          live[i]->seq = seq;
        }
      }
      finalSeq[w] = seq;
    });
  }

  // kernel-mode writers: read(2) from a pipe into live objects
  std::atomic<uint64_t> efaults{0}, kernWrites{0}, kernBad{0};
  for (int w = 0; w < kKernWriters; w++) {
    threads.emplace_back([&, w] {
      int fds[2];
      if (pipe(fds) != 0) {
        perror("pipe");
        abort();
      }
      char buf[kKernLen];
      uint64_t seq = 0;
      std::mt19937_64 r(w + 1);
      while (!stop.load(std::memory_order_relaxed)) {
        // kernel writer w owns objects with index % kKernWriters == w
        size_t i = (r() % (live.size() / kKernWriters)) * kKernWriters + w;
        Obj *o = live[i];
        seq++;
        fillKern(buf, w, seq);
        if (write(fds[1], buf, kKernLen) != (ssize_t)kKernLen) {
          perror("pipe write");
          abort();
        }
        ssize_t n;
        while ((n = read(fds[0], o->kern, kKernLen)) < 0 && errno == EFAULT) {
          efaults.fetch_add(1, std::memory_order_relaxed);
          // data stays in the pipe on EFAULT before any bytes are copied;
          // retry until the mesh completes.
        }
        if (n != (ssize_t)kKernLen) {
          fprintf(stderr, "read returned %zd (errno %d)\n", n, errno);
          abort();
        }
        if (memcmp(o->kern, buf, kKernLen) != 0) {
          kernBad.fetch_add(1);
        }
        o->kernSeq = seq;
        kernWrites.fetch_add(1, std::memory_order_relaxed);
      }
      close(fds[0]);
      close(fds[1]);
    });
  }

  // churn so meshing remains "effective" and freeFor paths run concurrently
  threads.emplace_back([&] {
    std::vector<void *> v;
    std::mt19937_64 r(7);
    while (!stop.load(std::memory_order_relaxed)) {
      v.push_back(malloc(kObjSize));
      if (v.size() > 4096) {
        size_t j = r() % v.size();
        free(v[j]);
        v[j] = v.back();
        v.pop_back();
      }
    }
    for (void *p : v)
      free(p);
  });

  // mesh passes
  size_t passes = 0;
  const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  while (std::chrono::steady_clock::now() < end) {
    size_t dummy = 0, len = sizeof(dummy);
    mesh_mallctl("mesh.compact", &dummy, &len, nullptr, 0);
    passes++;
  }
  const size_t rssAfter = rssKiB();

  stop = true;
  for (auto &t : threads)
    t.join();

  // verify: every user-mode object holds its owner's final seq
  size_t lostUser = 0;
  for (size_t i = 0; i < live.size(); i++) {
    if (live[i]->owner != i % kUserWriters || live[i]->seq != finalSeq[i % kUserWriters])
      lostUser++;
  }

  printf("mesh passes: %zu, rss after: %zu MiB (%.0f%% of before)\n", passes, rssAfter / 1024,
         100.0 * rssAfter / (rssBefore ? rssBefore : 1));
  printf("user writes: final seqs %lu/%lu/%lu/%lu, lost objects: %zu\n", (unsigned long)finalSeq[0],
         (unsigned long)finalSeq[1], (unsigned long)finalSeq[2], (unsigned long)finalSeq[3], lostUser);
  printf("kernel writes: %lu, EFAULT retries: %lu, corrupt: %lu\n", (unsigned long)kernWrites.load(),
         (unsigned long)efaults.load(), (unsigned long)kernBad.load());

  bool ok = lostUser == 0 && kernBad.load() == 0 && passes > 0;
  if (mode == 2 && efaults.load() != 0) {
    printf("FAIL: EFAULT under the kernel-mode uffd barrier\n");
    ok = false;
  }
  if (rssAfter >= rssBefore) {
    printf("WARN: meshing did not reduce RSS\n");
  }
  printf("%s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}
