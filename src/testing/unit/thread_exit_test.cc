// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2024 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "internal.h"
#include "runtime.h"

using namespace std;
using namespace mesh;

// We need to wrap pthread_create so that we can safely implement a
// stop-the-world quiescent period for the copy/mremap phase of meshing
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

// Test for race conditions in DeleteHeap when multiple threads exit simultaneously.
//
// The DeleteHeap function manipulates a linked list of ThreadLocalHeap objects
// (_threadLocalHeaps, _next, _prev). If multiple threads exit at the same time,
// their pthread_key destructors (DestroyThreadLocalHeap) may race with each other.
//
// This test creates many short-lived threads that:
// 1. Do one allocation (to trigger ThreadLocalHeap creation)
// 2. Synchronize at a barrier
// 3. Exit simultaneously (triggering concurrent DeleteHeap calls)

class ThreadExitRaceTest {
public:
  static void runTest() {
    constexpr size_t kNumThreads = 64;
    constexpr size_t kIterations = 100;

    for (size_t iter = 0; iter < kIterations; iter++) {
      atomic<size_t> readyCount{0};
      atomic<bool> go{false};
      atomic<size_t> doneCount{0};

      vector<thread> threads;
      threads.reserve(kNumThreads);

      for (size_t i = 0; i < kNumThreads; i++) {
        threads.emplace_back([&readyCount, &go, &doneCount]() {
          // Do one allocation to trigger ThreadLocalHeap creation
          void *p = ::malloc(64);
          if (p) {
            memset(p, 0x42, 64);
            ::free(p);
          }

          // Signal ready and wait for all threads
          readyCount.fetch_add(1, memory_order_acq_rel);
          while (!go.load(memory_order_acquire)) {
            this_thread::yield();
          }

          // All threads exit simultaneously here
          // This triggers concurrent DestroyThreadLocalHeap -> DeleteHeap calls
          doneCount.fetch_add(1, memory_order_acq_rel);
        });
      }

      // Wait for all threads to be ready
      while (readyCount.load(memory_order_acquire) < kNumThreads) {
        this_thread::yield();
      }

      // Release all threads at once
      go.store(true, memory_order_release);

      // Join all threads
      for (auto &t : threads) {
        t.join();
      }

      ASSERT_EQ(doneCount.load(), kNumThreads) << "Not all threads completed at iteration " << iter;
    }
  }
};

// More aggressive test with rapid thread creation/destruction cycles
class RapidThreadChurnTest {
public:
  static void runTest() {
    constexpr size_t kNumThreads = 32;
    constexpr size_t kChurnCycles = 50;
    constexpr size_t kAllocsPerThread = 10;

    for (size_t cycle = 0; cycle < kChurnCycles; cycle++) {
      vector<thread> threads;
      threads.reserve(kNumThreads);

      atomic<size_t> barrier{0};

      for (size_t i = 0; i < kNumThreads; i++) {
        threads.emplace_back([&barrier, i]() {
          // Do several allocations
          vector<void *> ptrs;
          for (size_t j = 0; j < kAllocsPerThread; j++) {
            void *p = ::malloc(32 + (i % 8) * 16);
            if (p) {
              ptrs.push_back(p);
            }
          }

          // Free them
          for (void *p : ptrs) {
            ::free(p);
          }

          // Wait at barrier before exiting
          barrier.fetch_add(1, memory_order_acq_rel);
          while (barrier.load(memory_order_acquire) < kNumThreads) {
            this_thread::yield();
          }
        });
      }

      for (auto &t : threads) {
        t.join();
      }
    }
  }
};

// Test with varying thread counts to hit different linked list states
class VariableThreadCountTest {
public:
  static void runTest() {
    for (size_t numThreads = 2; numThreads <= 128; numThreads *= 2) {
      for (size_t rep = 0; rep < 10; rep++) {
        atomic<size_t> readyCount{0};
        atomic<bool> go{false};

        vector<thread> threads;
        threads.reserve(numThreads);

        for (size_t i = 0; i < numThreads; i++) {
          threads.emplace_back([&readyCount, &go]() {
            void *p = ::malloc(48);
            if (p) {
              ::free(p);
            }

            readyCount.fetch_add(1, memory_order_acq_rel);
            while (!go.load(memory_order_acquire)) {
              this_thread::yield();
            }
          });
        }

        while (readyCount.load(memory_order_acquire) < numThreads) {
          this_thread::yield();
        }

        go.store(true, memory_order_release);

        for (auto &t : threads) {
          t.join();
        }
      }
    }
  }
};

// Stress test with long duration
class ThreadExitStressTest {
public:
  static void runTest() {
    constexpr int kTestDurationMs = 3000;
    constexpr size_t kBatchSize = 16;

    atomic<bool> shouldStop{false};
    atomic<size_t> totalBatches{0};

    auto stressWorker = [&]() {
      while (!shouldStop.load(memory_order_relaxed)) {
        atomic<size_t> barrier{0};
        vector<thread> batch;
        batch.reserve(kBatchSize);

        for (size_t i = 0; i < kBatchSize; i++) {
          batch.emplace_back([&barrier, kBatchSize]() {
            void *p = ::malloc(64);
            if (p) {
              memset(p, 0xAB, 64);
              ::free(p);
            }

            barrier.fetch_add(1, memory_order_acq_rel);
            while (barrier.load(memory_order_acquire) < kBatchSize) {
              this_thread::yield();
            }
          });
        }

        for (auto &t : batch) {
          t.join();
        }

        totalBatches.fetch_add(1, memory_order_relaxed);
      }
    };

    // Run stress workers from multiple threads
    vector<thread> stressors;
    for (int i = 0; i < 4; i++) {
      stressors.emplace_back(stressWorker);
    }

    this_thread::sleep_for(chrono::milliseconds(kTestDurationMs));
    shouldStop = true;

    for (auto &s : stressors) {
      s.join();
    }

    ASSERT_GT(totalBatches.load(), 0UL) << "No batches completed - test may have failed early";
  }
};

TEST(ThreadExitTest, ConcurrentExit) {
  ThreadExitRaceTest::runTest();
}

TEST(ThreadExitTest, RapidChurn) {
  RapidThreadChurnTest::runTest();
}

TEST(ThreadExitTest, VariableThreadCount) {
  VariableThreadCountTest::runTest();
}

TEST(ThreadExitTest, StressTest) {
  ThreadExitStressTest::runTest();
}
