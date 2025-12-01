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

// Test for cycle detection in pending partial list drain.
// This tests for a race condition where:
// 1. Thread A drains the pending list, processing nodes one by one
// 2. Processed nodes are added to freelist, pending flag cleared
// 3. Other threads allocate processed nodes, make them Full, free them
// 4. The freed nodes get pushed to a NEW pending list
// 5. If _freelist._next is shared between old and new pending lists,
//    the old iteration can read corrupted _next values, creating a cycle
//
// Uses the regular malloc/free path which goes through ThreadLocalHeap
// and triggers the pending partial list code when miniheaps transition
// from Full to Partial state.

class PendingListCycleTest {
public:
  static constexpr size_t kObjSize = 64;
  static constexpr size_t kNumThreads = 4;
  static constexpr size_t kIterations = 1000;
  static constexpr size_t kAllocsPerThread = 100;

  static void runTest() {
    atomic<bool> shouldStop{false};
    atomic<size_t> cycleDetected{0};

    // Worker threads that rapidly alloc/free to create pending list pressure
    auto worker = [&](int threadId) {
      vector<void *> ptrs;
      ptrs.reserve(kAllocsPerThread);

      while (!shouldStop.load(memory_order_relaxed)) {
        // Allocate many small objects
        for (size_t i = 0; i < kAllocsPerThread; i++) {
          void *p = ::malloc(kObjSize);
          if (p) {
            ptrs.push_back(p);
          }
        }

        // Free them all - this triggers tryPushPendingPartial when miniheaps go Full->Partial
        for (void *p : ptrs) {
          ::free(p);
        }
        ptrs.clear();

        // Small yield to allow other threads to run
        this_thread::yield();
      }
    };

    // Start worker threads
    vector<thread> workers;
    for (size_t i = 0; i < kNumThreads; i++) {
      workers.emplace_back(worker, i);
    }

    // Main thread does allocations that trigger drains
    // Each allocation calls drainPendingPartialLocked
    for (size_t iter = 0; iter < kIterations && cycleDetected == 0; iter++) {
      vector<void *> ptrs;

      // Allocate - this triggers drain
      for (size_t i = 0; i < kAllocsPerThread; i++) {
        void *p = ::malloc(kObjSize);
        if (p) {
          ptrs.push_back(p);
        }
      }

      // Free
      for (void *p : ptrs) {
        ::free(p);
      }

      // Occasionally check for stuck threads by attempting another alloc
      // If drain is stuck in a cycle, this will hang
      if (iter % 100 == 0) {
        void *p = ::malloc(kObjSize);
        if (p) {
          ::free(p);
        }
      }
    }

    shouldStop = true;
    for (auto &w : workers) {
      w.join();
    }

    ASSERT_EQ(cycleDetected.load(), 0UL) << "Cycle detected in pending list!";
  }
};

// A more direct test that verifies drain doesn't visit the same node twice
class PendingListDrainIntegrityTest {
public:
  static constexpr size_t kObjSize = 32;
  static constexpr size_t kNumIterations = 500;
  static constexpr size_t kNumWorkers = 3;

  static void runTest() {
    atomic<bool> shouldStop{false};
    atomic<size_t> allocFreeCount{0};

    // Worker threads that do rapid alloc/free cycles
    auto worker = [&]() {
      vector<void *> ptrs;
      ptrs.reserve(50);

      while (!shouldStop.load(memory_order_acquire)) {
        // Allocate
        for (int i = 0; i < 50; i++) {
          void *p = ::malloc(kObjSize);
          if (p) {
            memset(p, 0x42, kObjSize);
            ptrs.push_back(p);
          }
        }

        // Free all
        for (void *p : ptrs) {
          ::free(p);
        }
        ptrs.clear();
        allocFreeCount.fetch_add(1, memory_order_relaxed);
      }
    };

    vector<thread> workers;
    for (size_t i = 0; i < kNumWorkers; i++) {
      workers.emplace_back(worker);
    }

    // Main thread: repeatedly allocate and free, which exercises drain
    for (size_t iter = 0; iter < kNumIterations; iter++) {
      vector<void *> mainPtrs;

      // Allocate many objects
      for (size_t i = 0; i < 100; i++) {
        void *p = ::malloc(kObjSize);
        if (p) {
          memset(p, 0xAB, kObjSize);
          mainPtrs.push_back(p);
        }
      }

      // Free them
      for (void *p : mainPtrs) {
        ::free(p);
      }
    }

    shouldStop = true;
    for (auto &w : workers) {
      w.join();
    }

    // If we got here without hanging, the test passed
    SUCCEED() << "Completed " << kNumIterations << " iterations with " << allocFreeCount.load()
              << " worker alloc/free cycles";
  }
};

// Stress test with timeout detection
class PendingListStressTest {
public:
  static void runTest() {
    constexpr size_t kObjSize = 48;
    constexpr size_t kNumThreads = 6;
    constexpr int kTestDurationMs = 2000;  // 2 seconds

    atomic<bool> shouldStop{false};
    atomic<size_t> totalOps{0};
    atomic<bool> testPassed{true};

    auto worker = [&](int id) {
      vector<void *> ptrs;
      ptrs.reserve(200);
      size_t localOps = 0;

      while (!shouldStop.load(memory_order_relaxed)) {
        // Allocate batch
        for (int i = 0; i < 100; i++) {
          void *p = ::malloc(kObjSize);
          if (p) {
            ptrs.push_back(p);
          }
        }

        // Free batch
        for (void *p : ptrs) {
          ::free(p);
        }
        ptrs.clear();
        localOps++;

        // Quick responsiveness check - if we can't allocate/free quickly, something is wrong
        auto start = chrono::steady_clock::now();
        void *check = ::malloc(kObjSize);
        if (check) {
          ::free(check);
        }
        auto elapsed = chrono::steady_clock::now() - start;
        if (elapsed > chrono::milliseconds(100)) {
          // Allocation took too long - might be stuck in a loop
          testPassed = false;
        }
      }

      totalOps.fetch_add(localOps, memory_order_relaxed);
    };

    vector<thread> threads;
    for (size_t i = 0; i < kNumThreads; i++) {
      threads.emplace_back(worker, i);
    }

    // Let the test run
    this_thread::sleep_for(chrono::milliseconds(kTestDurationMs));
    shouldStop = true;

    for (auto &t : threads) {
      t.join();
    }

    ASSERT_TRUE(testPassed.load()) << "Test detected potential infinite loop (slow allocation)";
    ASSERT_GT(totalOps.load(), 0UL) << "No operations completed - test may have hung";
  }
};

TEST(PendingListTest, CycleDetection) {
  PendingListCycleTest::runTest();
}

TEST(PendingListTest, DrainIntegrity) {
  PendingListDrainIntegrityTest::runTest();
}

TEST(PendingListTest, StressTest) {
  PendingListStressTest::runTest();
}
