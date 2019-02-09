#include <assert.h>
#include <ctype.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <atomic>

#include "rng/mwc.h"

#define ATTRIBUTE_ALIGNED(s) __attribute__((aligned(s)))
#define CACHELINE_SIZE 64
#define CACHELINE_ALIGNED ATTRIBUTE_ALIGNED(CACHELINE_SIZE)

static int64_t QueryPerformanceCounter() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec * 1000000L + tv.tv_usec;
}

static int64_t QueryPerformanceFrequency() {
  return 1000000L;
}

typedef void *VoidFunction(void *);
void _beginthread(VoidFunction func, void *func_arg) {
  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

  //  printf ("creating a thread.\n");
  int v = pthread_create(&thread, &attr, func, func_arg);
  //  printf ("v = %d\n", v);
}

/* Test driver for memory allocators           */
/* Author: Paul Larson, palarson@microsoft.com */
#define MAX_THREADS 100
#define MAX_BLOCKS 20000000

static std::atomic_bool stopflag;

struct ThreadData {
  int threadno{0};   // 4 -- 4
  int NumBlocks{0};  // 4 -- 8
  int seed;          // 4 -- 12

  int min_size{0};  // 4 -- 16
  int max_size{0};  // 4 -- 20

  int asize{0};  // 4 -- 24

  char **array{nullptr};  // 8 -- 32
  int *blksize{nullptr};  // 8 -- 40

  uint64_t cAllocs{0};        // 8 -- 48
  uint64_t cFrees{0};         // 8 -- 56
  uint64_t cBytesAlloced{0};  // 8 -- 64

  MWC local_rng;  // 24 -- 88

  int cThreads{0};              // 4 -- 92
  std::atomic_int finished{0};  // 4 -- 96

  ThreadData(MWC &rng) : seed(rng.next()), local_rng(seed, seed) {
  }
} CACHELINE_ALIGNED;

static_assert(sizeof(ThreadData) == 128, "ThreadData unexpected size");

void runthreads(MWC &rng, long sleep_cnt, int min_threads, int max_threads, int chperthread, int num_rounds);
static void warmup(MWC &rng, char **blkp, int num_chunks);
static void *exercise_heap(void *pinput);
uint64_t CountReservedSpace();

char **blkp = new char *[MAX_BLOCKS];
int *blksize = new int[MAX_BLOCKS];
long seqlock = 0;
int min_size = 10, max_size = 500;
int num_threads;
uint64_t init_space;

int main(int argc, char *argv[]) {
  int min_threads, max_threads;
  int num_rounds;
  int chperthread;
  unsigned seed = 12345;
  int num_chunks = 10000;
  long sleep_cnt;

  if (argc > 7) {
    sleep_cnt = atoi(argv[1]);
    min_size = atoi(argv[2]);
    max_size = atoi(argv[3]);
    chperthread = atoi(argv[4]);
    num_rounds = atoi(argv[5]);
    seed = atoi(argv[6]);
    max_threads = atoi(argv[7]);
    min_threads = max_threads;
    printf("sleep=%ld min=%d max=%d per_thread=%d num_rounds=%d seed=%d max_threads=%d min_threads=%d\n", sleep_cnt,
           min_size, max_size, chperthread, num_rounds, seed, max_threads, min_threads);
  } else {
    printf("\nMulti-threaded test driver \n");
    printf("C++ version (new and delete)\n");
    printf("runtime (sec): ");
    scanf("%ld", &sleep_cnt);

    printf("chunk size (min,max): ");
    scanf("%d %d", &min_size, &max_size);
    //#ifdef _MT
    printf("threads (min, max):   ");
    scanf("%d %d", &min_threads, &max_threads);
    printf("chunks/thread:  ");
    scanf("%d", &chperthread);
    printf("no of rounds:   ");
    scanf("%d", &num_rounds);
    num_chunks = max_threads * chperthread;
    printf("random seed:    ");
    scanf("%d", &seed);
  }

  if (num_chunks > MAX_BLOCKS) {
    printf("Max %d chunks - exiting\n", MAX_BLOCKS);
    return (1);
  }

  MWC rng{seed, seed};

  runthreads(rng, sleep_cnt, min_threads, max_threads, chperthread, num_rounds);

  return 0;
}

void runthreads(MWC &rng, long sleep_cnt, int min_threads, int max_threads, int chperthread, int num_rounds) {
  ThreadData *de_area =
      reinterpret_cast<ThreadData *>(aligned_alloc(sizeof(ThreadData), sizeof(ThreadData) * max_threads));

  for (size_t i = 0; i < max_threads; i++) {
    new ((void *)&de_area[i]) ThreadData(rng);
  }

  ThreadData *pdea;
  int nperthread;
  int sum_threads;
  unsigned long sum_allocs;
  unsigned long sum_frees;
  double duration;
  long ticks_per_sec;
  long start_cnt, end_cnt;
  int64_t ticks;
  double rate_1 = 0, rate_n;
  double reqd_space;
  uint64_t used_space;
  int prevthreads;
  int i;

  ticks_per_sec = QueryPerformanceFrequency();

  pdea = &de_area[0];

  prevthreads = 0;
  for (num_threads = min_threads; num_threads <= max_threads; num_threads++) {
    warmup(rng, &blkp[prevthreads * chperthread], (num_threads - prevthreads) * chperthread);

    nperthread = chperthread;
    stopflag = false;

    for (i = 0; i < num_threads; i++) {
      de_area[i].threadno = i + 1;
      de_area[i].NumBlocks = num_rounds * nperthread;
      de_area[i].array = &blkp[i * nperthread];
      de_area[i].blksize = &blksize[i * nperthread];
      de_area[i].asize = nperthread;
      de_area[i].min_size = min_size;
      de_area[i].max_size = max_size;
      de_area[i].finished.store(0, std::memory_order::memory_order_release);

      _beginthread(exercise_heap, &de_area[i]);
    }

    start_cnt = QueryPerformanceCounter();

    // printf ("Sleeping for %ld seconds.\n", sleep_cnt);
    sleep(sleep_cnt);

    stopflag = true;

    for (i = 0; i < num_threads; i++) {
      while (!de_area[i].finished.load(std::memory_order::memory_order_acquire)) {
        sched_yield();
      }
    }

    end_cnt = QueryPerformanceCounter();

    sum_frees = sum_allocs = 0;
    sum_threads = 0;
    for (i = 0; i < num_threads; i++) {
      sum_allocs += de_area[i].cAllocs;
      sum_frees += de_area[i].cFrees;
      sum_threads += de_area[i].cThreads;
    }

    ticks = end_cnt - start_cnt;
    duration = (double)ticks / ticks_per_sec;

    for (i = 0; i < num_threads; i++) {
      if (!de_area[i].finished.load(std::memory_order::memory_order_acquire)) {
        printf("Thread at %d not finished\n", i);
      }
    }

    rate_n = sum_allocs / duration;
    if (rate_1 == 0) {
      rate_1 = rate_n;
    }

    reqd_space = (0.5 * (min_size + max_size) * num_threads * chperthread);
    // used_space = CountReservedSpace() - init_space;
    used_space = 0;

    printf("Throughput = %8.0f operations per second.\n", sum_allocs / duration);

    sleep(5);  // wait 5 sec for old threads to die

    prevthreads = num_threads;

    printf("Done sleeping...\n");
  }
  free(de_area);
}

static void *exercise_heap(void *pinput) {
  ThreadData *pdea = (ThreadData *)pinput;

  if (stopflag) {
    return 0;
  }

  pdea->finished.store(0, std::memory_order::memory_order_release);
  pdea->cThreads++;
  const int range = pdea->max_size - pdea->min_size;

  /* allocate NumBlocks chunks of random size */
  for (size_t cblks = 0; cblks < pdea->NumBlocks; cblks++) {
    const int victim = pdea->local_rng.next() % pdea->asize;
    free(pdea->array[victim]);
    pdea->cFrees++;

    int64_t blk_size = pdea->min_size;
    if (range != 0) {
      blk_size += pdea->local_rng.next() % range;
    }

    pdea->array[victim] = (char *)malloc(blk_size);

    pdea->blksize[victim] = blk_size;
    assert(pdea->array[victim] != NULL);

    pdea->cAllocs++;

    /* Write something! */

    volatile char *chptr = ((char *)pdea->array[victim]);
    *chptr++ = 'a';
    volatile char ch = *((char *)pdea->array[victim]);
    *chptr = 'b';

    if (stopflag) {
      break;
    }
  }

  //  	printf("Thread %u terminating: %d allocs, %d frees\n",
  //		      pdea->threadno, pdea->cAllocs, pdea->cFrees) ;
  pdea->finished.store(1, std::memory_order::memory_order_release);

  if (!stopflag) {
    _beginthread(exercise_heap, pdea);
  } else {
    printf("thread stopping.\n");
  }
  pthread_exit(NULL);
  return 0;
}

static void warmup(MWC &rng, char **blkp, int num_chunks) {
  int cblks;
  int victim;
  int blk_size;
  void *tmp;

  for (cblks = 0; cblks < num_chunks; cblks++) {
    if (min_size == max_size) {
      blk_size = min_size;
    } else {
      blk_size = min_size + rng.next() % (max_size - min_size);
    }
    blkp[cblks] = (char *)malloc(blk_size);
    blksize[cblks] = blk_size;
    assert(blkp[cblks] != NULL);
  }

  /* generate a random permutation of the chunks */
  for (cblks = num_chunks; cblks > 0; cblks--) {
    victim = rng.next() % cblks;
    tmp = blkp[victim];
    blkp[victim] = blkp[cblks - 1];
    blkp[cblks - 1] = (char *)tmp;
  }

  for (cblks = 0; cblks < 4 * num_chunks; cblks++) {
    victim = rng.next() % num_chunks;
    free(blkp[victim]);

    if (max_size == min_size) {
      blk_size = min_size;
    } else {
      blk_size = min_size + rng.next() % (max_size - min_size);
    }
    blkp[victim] = (char *)malloc(blk_size);
    blksize[victim] = blk_size;
    assert(blkp[victim] != NULL);
  }
}
