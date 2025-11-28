// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2024 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Larson benchmark - ported from mimalloc-bench
// Original author: Paul Larson, palarson@microsoft.com
//
// This benchmark stress-tests multi-threaded allocation by having each thread
// repeatedly free a random block and allocate a new random-sized block.
// This creates a pattern where threads frequently free memory that was
// allocated by other threads (after thread-local caches are exhausted),
// which exercises the "remote free" path in allocators.

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

// Configuration
static constexpr int kMaxThreads = 100;
static constexpr int kMaxBlocks = 1000000;

// Global state
static std::atomic<bool> g_stop_flag{false};

// Larson's portable RNG - must match original exactly for reproducibility
struct Lran2State {
  int64_t x;
  int64_t y;
  int64_t v[97];
};

static constexpr int64_t kLran2Max = 714025L;
static constexpr int64_t kIA = 1366L;
static constexpr int64_t kIC = 150889L;

static void lran2_init(Lran2State *d, int64_t seed) {
  int64_t x = (kIC - seed) % kLran2Max;
  if (x < 0)
    x = -x;
  for (int j = 0; j < 97; j++) {
    x = (kIA * x + kIC) % kLran2Max;
    d->v[j] = x;
  }
  d->x = (kIA * x + kIC) % kLran2Max;
  d->y = d->x;
}

static int64_t lran2(Lran2State *d) {
  int j = (d->y % 97);
  d->y = d->v[j];
  d->x = (kIA * d->x + kIC) % kLran2Max;
  d->v[j] = d->x;
  return d->y;
}

// Per-thread data
struct ThreadData {
  int thread_no;
  int num_blocks;
  int seed;
  int min_size;
  int max_size;

  char **array;
  size_t *blksize;
  int array_size;

  int64_t alloc_count;
  int64_t free_count;
  int64_t thread_count;

  std::atomic<bool> finished;
  Lran2State rgen;
};

// Global arrays
static char *g_blocks[kMaxBlocks];
static size_t g_block_sizes[kMaxBlocks];
static Lran2State g_rgen;
static int g_min_size = 10;
static int g_max_size = 500;

// High-resolution timing
static int64_t get_time_usec() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<int64_t>(tv.tv_sec) * 1000000L + tv.tv_usec;
}

// The core benchmark loop - runs in each thread
// This is the hot path that we're optimizing for
static void *exercise_heap(void *pinput) {
  if (g_stop_flag.load(std::memory_order_relaxed)) {
    return nullptr;
  }

  ThreadData *data = static_cast<ThreadData *>(pinput);
  data->finished.store(false, std::memory_order_relaxed);
  data->thread_count++;

  const int range = data->max_size - data->min_size;

  // Allocate and free num_blocks chunks of random size
  for (int i = 0; i < data->num_blocks; i++) {
    // Pick a random victim to free
    const int victim = lran2(&data->rgen) % data->array_size;

    // Free the old block
#if defined(SIZED_DELETE)
    operator delete[](data->array[victim], data->blksize[victim]);
#else
    delete[] data->array[victim];
#endif
    data->free_count++;

    // Allocate a new block with random size
    size_t blk_size;
    if (range == 0) {
      blk_size = data->min_size;
    } else {
      blk_size = data->min_size + lran2(&data->rgen) % range;
    }

    data->array[victim] = new char[blk_size];
    data->blksize[victim] = blk_size;
    assert(data->array[victim] != nullptr);
    data->alloc_count++;

    // Write something to ensure the memory is actually used
    volatile char *ptr = data->array[victim];
    ptr[0] = 'a';
    volatile char ch = ptr[0];
    (void)ch;
    if (blk_size > 1) {
      ptr[1] = 'b';
    }

    if (g_stop_flag.load(std::memory_order_relaxed)) {
      break;
    }
  }

  data->finished.store(true, std::memory_order_release);

  // If not stopped, spawn another iteration (like original)
  if (!g_stop_flag.load(std::memory_order_relaxed)) {
    pthread_t pt;
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setscope(&pa, PTHREAD_SCOPE_SYSTEM);
    pthread_create(&pt, &pa, exercise_heap, data);
    pthread_attr_destroy(&pa);
  }

  return nullptr;
}

// Warmup phase - allocate initial blocks and shuffle them
static void warmup(char **blkp, size_t *blksize, int num_chunks) {
  // Initial allocation
  for (int i = 0; i < num_chunks; i++) {
    size_t blk_size;
    if (g_min_size == g_max_size) {
      blk_size = g_min_size;
    } else {
      blk_size = g_min_size + lran2(&g_rgen) % (g_max_size - g_min_size);
    }
    blkp[i] = new char[blk_size];
    blksize[i] = blk_size;
    assert(blkp[i] != nullptr);
  }

  // Generate a random permutation of the chunks
  for (int i = num_chunks; i > 0; i--) {
    const int victim = lran2(&g_rgen) % i;
    char *tmp = blkp[victim];
    size_t tmp_sz = blksize[victim];
    blkp[victim] = blkp[i - 1];
    blksize[victim] = blksize[i - 1];
    blkp[i - 1] = tmp;
    blksize[i - 1] = tmp_sz;
  }

  // Do some initial free/alloc cycles
  for (int i = 0; i < 4 * num_chunks; i++) {
    const int victim = lran2(&g_rgen) % num_chunks;

#if defined(SIZED_DELETE)
    operator delete[](blkp[victim], blksize[victim]);
#else
    delete[] blkp[victim];
#endif

    size_t blk_size;
    if (g_max_size == g_min_size) {
      blk_size = g_min_size;
    } else {
      blk_size = g_min_size + lran2(&g_rgen) % (g_max_size - g_min_size);
    }
    blkp[victim] = new char[blk_size];
    blksize[victim] = blk_size;
    assert(blkp[victim] != nullptr);
  }
}

// Main benchmark runner
static void run_threads(int sleep_sec, int num_threads, int chunks_per_thread, int num_rounds) {
  ThreadData thread_data[kMaxThreads] = {};

  // Warmup
  warmup(g_blocks, g_block_sizes, num_threads * chunks_per_thread);

  g_stop_flag.store(false, std::memory_order_release);

  // Initialize thread data and spawn threads
  for (int i = 0; i < num_threads; i++) {
    thread_data[i].thread_no = i + 1;
    thread_data[i].num_blocks = num_rounds * chunks_per_thread;
    thread_data[i].array = &g_blocks[i * chunks_per_thread];
    thread_data[i].blksize = &g_block_sizes[i * chunks_per_thread];
    thread_data[i].array_size = chunks_per_thread;
    thread_data[i].min_size = g_min_size;
    thread_data[i].max_size = g_max_size;
    thread_data[i].seed = lran2(&g_rgen);
    thread_data[i].finished.store(false, std::memory_order_relaxed);
    thread_data[i].alloc_count = 0;
    thread_data[i].free_count = 0;
    thread_data[i].thread_count = 0;
    lran2_init(&thread_data[i].rgen, thread_data[i].seed);

    pthread_t pt;
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setscope(&pa, PTHREAD_SCOPE_SYSTEM);
    pthread_create(&pt, &pa, exercise_heap, &thread_data[i]);
    pthread_attr_destroy(&pa);
  }

  const int64_t start_time = get_time_usec();

  // Sleep for the specified duration
  sleep(sleep_sec);

  // Signal threads to stop
  g_stop_flag.store(true, std::memory_order_release);

  // Wait for all threads to finish
  for (int i = 0; i < num_threads; i++) {
    while (!thread_data[i].finished.load(std::memory_order_acquire)) {
      sched_yield();
    }
  }

  const int64_t end_time = get_time_usec();
  const double duration = static_cast<double>(end_time - start_time) / 1000000.0;

  // Sum up statistics
  int64_t total_allocs = 0;
  int64_t total_frees = 0;
  for (int i = 0; i < num_threads; i++) {
    total_allocs += thread_data[i].alloc_count;
    total_frees += thread_data[i].free_count;
  }

  const double throughput = static_cast<double>(total_allocs) / duration;
  const double relative_time = 1.0e9 / throughput;

  printf("Throughput = %.0f operations per second, relative time: %.3fs.\n", throughput, relative_time);

  // Brief sleep to let threads fully terminate
  usleep(500000);
  printf("Done sleeping...\n");
}

static void print_usage(const char *prog) {
  fprintf(stderr, "Usage: %s <sleep_sec> <min_size> <max_size> <chunks_per_thread> <num_rounds> <seed> <num_threads>\n",
          prog);
  fprintf(stderr, "\nExample (mimalloc-bench larsonN-sized equivalent):\n");
  fprintf(stderr, "  %s 5 8 1000 5000 100 4141 8\n", prog);
}

int main(int argc, char *argv[]) {
  if (argc != 8) {
    print_usage(argv[0]);
    return 1;
  }

  const int sleep_sec = atoi(argv[1]);
  g_min_size = atoi(argv[2]);
  g_max_size = atoi(argv[3]);
  const int chunks_per_thread = atoi(argv[4]);
  const int num_rounds = atoi(argv[5]);
  const unsigned int seed = atoi(argv[6]);
  const int num_threads = atoi(argv[7]);

  if (num_threads > kMaxThreads) {
    fprintf(stderr, "Max %d threads supported\n", kMaxThreads);
    return 1;
  }

  if (num_threads * chunks_per_thread > kMaxBlocks) {
    fprintf(stderr, "Max %d total blocks supported\n", kMaxBlocks);
    return 1;
  }

  lran2_init(&g_rgen, seed);
  run_threads(sleep_sec, num_threads, chunks_per_thread, num_rounds);

  return 0;
}
