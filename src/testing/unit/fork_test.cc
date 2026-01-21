// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2024 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Test for fork() handling with the mesh allocator.
//
// The mesh allocator uses pthread_atfork handlers to prepare for fork:
// 1. prepareForFork: Acquires locks and marks the arena as read-only
// 2. afterForkParent: Waits for child to signal, restores permissions, unlocks
// 3. afterForkChild: Creates new backing file, remaps arena, unlocks
//
// This test verifies that fork() works correctly when:
// - Multiple threads are actively allocating/freeing memory
// - The meshing feature is enabled (arena is file-backed)

#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#include <spawn.h>
#include <sched.h>

#include "gtest/gtest.h"

#include "internal.h"
#include "runtime.h"

using namespace std;
using namespace mesh;

// We need to wrap pthread_create so that mesh can track threads
#if defined(__APPLE__) || defined(__FreeBSD__)
#define PTHREAD_CREATE_THROW
#else
#define PTHREAD_CREATE_THROW throw()
#endif

extern "C" int __attribute__((weak)) pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                                    mesh::PthreadFn startRoutine, void *arg) PTHREAD_CREATE_THROW {
  if (getPageSize() == 4096) {
    return mesh::runtime<4096>().createThread(thread, attr, startRoutine, arg);
  } else {
    return mesh::runtime<16384>().createThread(thread, attr, startRoutine, arg);
  }
}

// Test that runtime PID is properly initialized
// This is critical for the segfault handler to correctly distinguish
// between parent and child processes during fork
TEST(ForkTest, RuntimePidInitialized) {
  pid_t currentPid = getpid();
  pid_t runtimePid = getPageSize() == 4096 ? runtime<4096>().pid() : runtime<16384>().pid();
  EXPECT_EQ(runtimePid, currentPid) << "Runtime PID should be initialized to current process PID";
}

// Test basic fork functionality with mesh allocator
TEST(ForkTest, BasicFork) {
  // Allocate some memory before fork
  void *ptr = malloc(64);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0x42, 64);

  pid_t pid = fork();
  if (pid == 0) {
    // Child process
    // Verify we can still access the memory
    EXPECT_EQ(reinterpret_cast<unsigned char *>(ptr)[0], 0x42);

    // Allocate new memory in child
    void *child_ptr = malloc(128);
    EXPECT_NE(child_ptr, nullptr);
    if (child_ptr) {
      memset(child_ptr, 0x55, 128);
      free(child_ptr);
    }

    free(ptr);
    _exit(0);  // Exit child successfully
  } else {
    ASSERT_GT(pid, 0) << "fork() failed";

    // Parent process
    int status;
    pid_t result = waitpid(pid, &status, 0);
    ASSERT_EQ(result, pid);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0);

    // Parent can still use its memory
    EXPECT_EQ(reinterpret_cast<unsigned char *>(ptr)[0], 0x42);
    free(ptr);
  }
}

// Test fork with concurrent allocations
// This tests the scenario where other threads are allocating while fork happens
class ForkWithAllocationsTest {
public:
  static constexpr size_t kNumWorkerThreads = 4;
  static constexpr size_t kNumForks = 10;
  static constexpr size_t kAllocationsPerThread = 100;

  static void runTest() {
    atomic<bool> stopWorkers{false};
    atomic<size_t> totalAllocations{0};

    // Start worker threads that continuously allocate/free
    vector<thread> workers;
    for (size_t i = 0; i < kNumWorkerThreads; i++) {
      workers.emplace_back([&stopWorkers, &totalAllocations, i]() {
        vector<void *> ptrs;
        ptrs.reserve(kAllocationsPerThread);

        while (!stopWorkers.load(memory_order_acquire)) {
          // Allocate some memory
          for (size_t j = 0; j < 10; j++) {
            size_t sz = 16 + (i * 8) + (j * 4);
            void *p = malloc(sz);
            if (p) {
              memset(p, static_cast<int>(i), min(sz, size_t(64)));
              if (ptrs.size() < kAllocationsPerThread) {
                ptrs.push_back(p);
              } else {
                free(p);
              }
              totalAllocations.fetch_add(1, memory_order_relaxed);
            }
          }

          // Free some memory
          while (ptrs.size() > kAllocationsPerThread / 2) {
            free(ptrs.back());
            ptrs.pop_back();
          }

          this_thread::yield();
        }

        // Cleanup
        for (void *p : ptrs) {
          free(p);
        }
      });
    }

    // Let workers run for a bit
    this_thread::sleep_for(chrono::milliseconds(10));

    // Do multiple forks while workers are running
    for (size_t i = 0; i < kNumForks; i++) {
      pid_t pid = fork();
      if (pid == 0) {
        // Child: do one allocation and exit
        void *p = malloc(64);
        if (p) {
          memset(p, 0xAA, 64);
          free(p);
        }
        _exit(0);
      } else {
        ASSERT_GT(pid, 0) << "fork() failed on iteration " << i;
        int status;
        pid_t result = waitpid(pid, &status, 0);
        ASSERT_EQ(result, pid);
        ASSERT_TRUE(WIFEXITED(status)) << "Child did not exit normally";
        ASSERT_EQ(WEXITSTATUS(status), 0) << "Child exited with error";
      }
    }

    // Stop workers
    stopWorkers.store(true, memory_order_release);
    for (auto &t : workers) {
      t.join();
    }

    // Verify we did some allocations
    EXPECT_GT(totalAllocations.load(), 0UL);
  }
};

TEST(ForkTest, ForkWithConcurrentAllocations) {
  ForkWithAllocationsTest::runTest();
}

// Test vfork + exec pattern (similar to how cargo/rust spawn processes)
TEST(ForkTest, VforkExec) {
  // Allocate some memory that will be shared with vfork child
  void *ptr = malloc(64);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0x42, 64);

  pid_t pid = vfork();
  if (pid == 0) {
    // Child: immediately exec to avoid undefined behavior in vfork
    // Use /bin/true as a simple command that exists on most systems
    execl("/bin/true", "true", nullptr);
    _exit(127);  // exec failed
  } else {
    ASSERT_GT(pid, 0) << "vfork() failed";

    int status;
    pid_t result = waitpid(pid, &status, 0);
    ASSERT_EQ(result, pid);
    ASSERT_TRUE(WIFEXITED(status));
    // Either exec succeeded (exit 0) or failed (exit 127)
    int exitCode = WEXITSTATUS(status);
    EXPECT_TRUE(exitCode == 0 || exitCode == 127);

    // Parent can still use its memory
    EXPECT_EQ(reinterpret_cast<unsigned char *>(ptr)[0], 0x42);
    free(ptr);
  }
}

// Test multiple rapid vfork+exec calls with concurrent allocations
// This mimics cargo's behavior of spawning many rustc processes
class VforkExecStressTest {
public:
  static constexpr size_t kNumWorkerThreads = 4;
  static constexpr size_t kNumVforks = 50;

  static void runTest() {
    atomic<bool> stopWorkers{false};
    atomic<size_t> vforkCount{0};

    // Start worker threads that continuously allocate/free
    vector<thread> workers;
    for (size_t i = 0; i < kNumWorkerThreads; i++) {
      workers.emplace_back([&stopWorkers, i]() {
        vector<void *> ptrs;
        ptrs.reserve(100);

        while (!stopWorkers.load(memory_order_acquire)) {
          // Allocate
          for (size_t j = 0; j < 5; j++) {
            void *p = malloc(64 + i * 8);
            if (p) {
              memset(p, static_cast<int>(i), 64);
              if (ptrs.size() < 100) {
                ptrs.push_back(p);
              } else {
                free(p);
              }
            }
          }

          // Free some
          while (ptrs.size() > 50) {
            free(ptrs.back());
            ptrs.pop_back();
          }

          this_thread::yield();
        }

        for (void *p : ptrs) {
          free(p);
        }
      });
    }

    // Let workers start
    this_thread::sleep_for(chrono::milliseconds(5));

    // Do many vfork+exec calls
    for (size_t i = 0; i < kNumVforks; i++) {
      pid_t pid = vfork();
      if (pid == 0) {
        execl("/bin/true", "true", nullptr);
        _exit(127);
      } else {
        ASSERT_GT(pid, 0) << "vfork() failed on iteration " << i;
        int status;
        waitpid(pid, &status, 0);
        vforkCount.fetch_add(1, memory_order_relaxed);
      }
    }

    stopWorkers.store(true, memory_order_release);
    for (auto &t : workers) {
      t.join();
    }

    EXPECT_EQ(vforkCount.load(), kNumVforks);
  }
};

TEST(ForkTest, VforkExecWithConcurrentAllocations) {
  VforkExecStressTest::runTest();
}

// Test that fork works correctly even with meshing enabled
// This requires that prepareForFork/afterFork handlers work correctly
TEST(ForkTest, ForkPreservesMemoryContents) {
  // Allocate multiple blocks with different patterns
  constexpr size_t kNumBlocks = 10;
  vector<pair<void *, size_t>> blocks;

  for (size_t i = 0; i < kNumBlocks; i++) {
    size_t sz = 64 + i * 32;
    void *p = malloc(sz);
    ASSERT_NE(p, nullptr);
    memset(p, static_cast<int>(i), sz);
    blocks.push_back({p, sz});
  }

  pid_t pid = fork();
  if (pid == 0) {
    // Child: verify all blocks have correct contents
    for (size_t i = 0; i < blocks.size(); i++) {
      auto [ptr, sz] = blocks[i];
      unsigned char *bytes = reinterpret_cast<unsigned char *>(ptr);
      for (size_t j = 0; j < sz; j++) {
        if (bytes[j] != static_cast<unsigned char>(i)) {
          _exit(1);  // Memory corruption detected
        }
      }
      free(ptr);
    }
    _exit(0);
  } else {
    ASSERT_GT(pid, 0);
    int status;
    waitpid(pid, &status, 0);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 0) << "Child detected memory corruption";

    // Parent also verifies and frees
    for (size_t i = 0; i < blocks.size(); i++) {
      auto [ptr, sz] = blocks[i];
      unsigned char *bytes = reinterpret_cast<unsigned char *>(ptr);
      for (size_t j = 0; j < sz; j++) {
        EXPECT_EQ(bytes[j], static_cast<unsigned char>(i));
      }
      free(ptr);
    }
  }
}

// Test: Race between fork's mprotect and concurrent thread heap access
//
// This test reproduces the scenario where:
// 1. Multiple threads are actively WRITING to heap memory (not just allocating)
// 2. One thread calls fork()
// 3. Mesh's prepareForFork() does mprotect(arena, PROT_READ)
// 4. Other threads crash with SIGSEGV when they try to write
//
// This is different from ForkWithConcurrentAllocations which tests allocation
// during fork - this test specifically triggers the mprotect race by having
// threads continuously write to already-allocated memory.
class ForkRaceWithWritersTest {
public:
  static constexpr size_t kNumWriterThreads = 4;
  static constexpr size_t kAllocSize = 4096;
  static constexpr size_t kNumAllocs = 100;

  static void runTest() {
    atomic<bool> keepRunning{true};
    atomic<int> writersReady{0};
    atomic<bool> forkCompleted{false};

    // Start writer threads that continuously write to memory
    vector<thread> writers;
    for (size_t i = 0; i < kNumWriterThreads; i++) {
      writers.emplace_back([&keepRunning, &writersReady, &forkCompleted, i]() {
        // Allocate memory for this thread
        vector<void *> allocations;
        allocations.reserve(kNumAllocs);
        for (size_t j = 0; j < kNumAllocs; j++) {
          void *p = malloc(kAllocSize);
          ASSERT_NE(p, nullptr) << "Thread " << i << ": malloc failed";
          allocations.push_back(p);
        }

        writersReady.fetch_add(1, memory_order_release);

        // Continuously write to allocated memory until told to stop
        // This is the key difference from ForkWithConcurrentAllocations:
        // we're actively doing memset operations, not just malloc/free
        while (keepRunning.load(memory_order_acquire)) {
          for (size_t j = 0; j < kNumAllocs && keepRunning.load(memory_order_acquire); j++) {
            // Write pattern to memory - this will crash if mprotect made it read-only
            memset(allocations[j], static_cast<int>(i + j), kAllocSize);

            // Small delay to increase chance of race
            if (j % 10 == 0) {
              sched_yield();
            }
          }
        }

        // Cleanup
        for (void *p : allocations) {
          free(p);
        }
      });
    }

    // Wait for all writers to be ready
    while (writersReady.load(memory_order_acquire) < static_cast<int>(kNumWriterThreads)) {
      this_thread::sleep_for(chrono::microseconds(100));
    }

    // Let writers run and establish a pattern
    this_thread::sleep_for(chrono::milliseconds(10));

    // Do the fork while writers are actively writing
    pid_t pid = fork();
    if (pid == 0) {
      // Child: do one allocation and exec to verify fork worked
      void *p = malloc(64);
      if (p) {
        memset(p, 0xAA, 64);
        free(p);
      }

      // Try a simple exec
      execl("/bin/true", "true", nullptr);
      _exit(127);
    } else {
      ASSERT_GT(pid, 0) << "fork() failed";

      int status;
      pid_t result = waitpid(pid, &status, 0);
      ASSERT_EQ(result, pid);

      if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        FAIL() << "Child killed by signal " << sig << (sig == SIGSEGV ? " (SIGSEGV - likely mprotect race!)" : "");
      }

      ASSERT_TRUE(WIFEXITED(status)) << "Child did not exit normally";
      EXPECT_EQ(WEXITSTATUS(status), 0) << "Child exited with error";
    }

    // Stop writers
    keepRunning.store(false, memory_order_release);
    for (auto &t : writers) {
      t.join();
    }
  }
};

TEST(ForkTest, ForkRaceWithWriters) {
  // Run multiple times to increase chance of hitting the race
  for (int iteration = 0; iteration < 5; iteration++) {
    ForkRaceWithWritersTest::runTest();
  }
}

// Test posix_spawn behavior (no atfork handlers called)
// posix_spawn uses clone3(CLONE_VM|CLONE_VFORK) - child shares parent's address space
// This means no atfork handlers are called, which should work fine with mesh
TEST(ForkTest, PosixSpawn) {
  // Allocate some memory first
  void *ptr = malloc(64);
  ASSERT_NE(ptr, nullptr);
  memset(ptr, 0x42, 64);

  pid_t pid;
  char *const argv[] = {const_cast<char *>("true"), nullptr};
  extern char **environ;

  int ret = posix_spawnp(&pid, "true", nullptr, nullptr, argv, environ);
  ASSERT_EQ(ret, 0) << "posix_spawn failed: " << strerror(ret);

  int status;
  pid_t result = waitpid(pid, &status, 0);
  ASSERT_EQ(result, pid);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);

  // Verify parent memory is still intact
  EXPECT_EQ(reinterpret_cast<unsigned char *>(ptr)[0], 0x42);
  free(ptr);
}

// Test mixed posix_spawn and fork+exec calls
// This tests that alternating between the two works correctly
TEST(ForkTest, MixedSpawns) {
  for (int i = 0; i < 5; i++) {
    if (i % 2 == 0) {
      // posix_spawn
      pid_t pid;
      char *const argv[] = {const_cast<char *>("true"), nullptr};
      extern char **environ;

      int ret = posix_spawnp(&pid, "true", nullptr, nullptr, argv, environ);
      ASSERT_EQ(ret, 0) << "posix_spawn " << i << " failed: " << strerror(ret);

      int status;
      waitpid(pid, &status, 0);
      EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    } else {
      // fork+exec
      pid_t pid = fork();
      if (pid == 0) {
        execl("/bin/true", "true", nullptr);
        _exit(127);
      } else {
        ASSERT_GT(pid, 0) << "fork " << i << " failed";
        int status;
        waitpid(pid, &status, 0);
        EXPECT_TRUE(WIFEXITED(status));
      }
    }
  }
}

// Test posix_spawn with concurrent allocations
// Similar to ForkWithConcurrentAllocations but using posix_spawn
class PosixSpawnStressTest {
public:
  static constexpr size_t kNumWorkerThreads = 4;
  static constexpr size_t kNumSpawns = 20;

  static void runTest() {
    atomic<bool> stopWorkers{false};
    atomic<size_t> spawnCount{0};

    // Start worker threads that continuously allocate/free
    vector<thread> workers;
    for (size_t i = 0; i < kNumWorkerThreads; i++) {
      workers.emplace_back([&stopWorkers, i]() {
        vector<void *> ptrs;
        ptrs.reserve(100);

        while (!stopWorkers.load(memory_order_acquire)) {
          // Allocate
          for (size_t j = 0; j < 5; j++) {
            void *p = malloc(64 + i * 8);
            if (p) {
              memset(p, static_cast<int>(i), 64);
              if (ptrs.size() < 100) {
                ptrs.push_back(p);
              } else {
                free(p);
              }
            }
          }

          // Free some
          while (ptrs.size() > 50) {
            free(ptrs.back());
            ptrs.pop_back();
          }

          this_thread::yield();
        }

        for (void *p : ptrs) {
          free(p);
        }
      });
    }

    // Let workers start
    this_thread::sleep_for(chrono::milliseconds(5));

    // Do many posix_spawn calls
    extern char **environ;
    for (size_t i = 0; i < kNumSpawns; i++) {
      pid_t pid;
      char *const argv[] = {const_cast<char *>("true"), nullptr};

      int ret = posix_spawnp(&pid, "true", nullptr, nullptr, argv, environ);
      ASSERT_EQ(ret, 0) << "posix_spawn failed on iteration " << i;

      int status;
      waitpid(pid, &status, 0);
      spawnCount.fetch_add(1, memory_order_relaxed);
    }

    stopWorkers.store(true, memory_order_release);
    for (auto &t : workers) {
      t.join();
    }

    EXPECT_EQ(spawnCount.load(), kNumSpawns);
  }
};

TEST(ForkTest, PosixSpawnWithConcurrentAllocations) {
  PosixSpawnStressTest::runTest();
}
