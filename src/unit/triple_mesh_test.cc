// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <sched.h>
#include <stdint.h>
#include <stdlib.h>

#include <atomic>
#include <mutex>
#include <thread>

#include "gtest/gtest.h"

#include "internal.h"
#include "meshing.h"
#include "runtime.h"
#include "shuffle_vector.h"

using namespace std;
using namespace mesh;

static constexpr uint32_t StrLen = 128;
static constexpr uint32_t ObjCount = 32;

static char *s1;
static char *s2;
static char *s3;

static atomic<int> ShouldExit1;
static atomic<int> ShouldContinueTest1;
static atomic<int> ShouldExit2;
static atomic<int> ShouldContinueTest2;

#ifdef __APPLE__
#define PTHREAD_CREATE_THROW
#else
#define PTHREAD_CREATE_THROW throw()
#endif

// we need to wrap pthread_create so that we can safely implement a
// stop-the-world quiescent period for the copy/mremap phase of
// meshing -- copied from libmesh.cc
extern "C" int pthread_create(pthread_t *thread, const pthread_attr_t *attr, mesh::PthreadFn startRoutine,
                              void *arg) PTHREAD_CREATE_THROW;

static void writerThread1() {
  ShouldContinueTest1 = 1;

  for (size_t i = 1; i < numeric_limits<uint64_t>::max(); i++) {
    if (i % 1000000 == 0 && ShouldExit1)
      return;

    s1[0] = 'A';
    s2[0] = 'B';
  }

  debug("loop ended before ShouldExit\n");
}

static void writerThread2() {
  ShouldContinueTest2 = 1;

  for (size_t i = 1; i < numeric_limits<uint64_t>::max(); i++) {
    if (i % 1000000 == 0 && ShouldExit2)
      return;

    s1[0] = 'A';
    s3[0] = 'Z';
  }

  debug("loop ended before ShouldExit\n");
}

// shows up in strace logs, but otherwise does nothing
static inline void note(const char *note) {
  int _ __attribute__((unused)) = write(-1, note, strlen(note));
}

static void meshTestConcurrentWrite(bool invert1, bool invert2) {
  const auto tid = gettid();
  GlobalHeap &gheap = runtime().heap();

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);

  FixedArray<MiniHeap, 1> array{};

  // allocate three miniheaps for the same object size from our global heap
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh1 = array[0];
  array.clear();

  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh2 = array[0];
  array.clear();

  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh3 = array[0];
  array.clear();

  const auto sizeClass = mh1->sizeClass();
  ASSERT_EQ(SizeMap::SizeClass(StrLen), sizeClass);

  ASSERT_TRUE(mh1->isAttached());
  ASSERT_TRUE(mh2->isAttached());
  ASSERT_TRUE(mh3->isAttached());

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 3UL);

  // sanity checks
  ASSERT_TRUE(mh1 != mh2);
  ASSERT_TRUE(mh2 != mh3);
  ASSERT_EQ(mh1->maxCount(), mh2->maxCount());
  ASSERT_EQ(mh2->maxCount(), mh3->maxCount());
  ASSERT_EQ(mh1->maxCount(), ObjCount);

  // allocate two c strings, one from each miniheap at different offsets
  s1 = reinterpret_cast<char *>(mh1->mallocAt(gheap.arenaBegin(), 0));
  s2 = reinterpret_cast<char *>(mh2->mallocAt(gheap.arenaBegin(), ObjCount - 1));
  s3 = reinterpret_cast<char *>(mh3->mallocAt(gheap.arenaBegin(), 3));

  ASSERT_TRUE(s1 != nullptr);
  ASSERT_TRUE(s2 != nullptr);
  ASSERT_TRUE(s3 != nullptr);

  {
    const auto f1 = reinterpret_cast<char *>(mh1->mallocAt(gheap.arenaBegin(), 2));
    const auto f2 = reinterpret_cast<char *>(mh2->mallocAt(gheap.arenaBegin(), 2));
    const auto f3 = reinterpret_cast<char *>(mh3->mallocAt(gheap.arenaBegin(), 2));

    gheap.releaseMiniheapLocked(mh1, mh1->sizeClass());
    gheap.releaseMiniheapLocked(mh2, mh1->sizeClass());
    gheap.releaseMiniheapLocked(mh3, mh1->sizeClass());

    gheap.free(f1);
    gheap.free(f2);
    gheap.free(f3);
  }

  ASSERT_TRUE(!mh1->isAttached());
  ASSERT_TRUE(!mh2->isAttached());
  ASSERT_TRUE(!mh3->isAttached());

  // fill in the strings, set the trailing null byte
  memset(s1, 'A', StrLen);
  memset(s2, 'B', StrLen);
  memset(s3, 'Z', StrLen);
  s1[StrLen - 1] = 0;
  s2[StrLen - 1] = 0;
  s3[StrLen - 1] = 0;

  // copy these strings so we can check the contents after meshing
  char *v1 = strdup(s1);
  char *v2 = strdup(s2);
  char *v3 = strdup(s3);
  ASSERT_STREQ(s1, v1);
  ASSERT_STREQ(s2, v2);
  ASSERT_STREQ(s3, v3);

  ASSERT_EQ(mh1->inUseCount(), 1UL);
  ASSERT_EQ(mh2->inUseCount(), 1UL);
  ASSERT_EQ(mh3->inUseCount(), 1UL);

  if (invert1) {
    MiniHeap *tmp = mh1;
    mh1 = mh2;
    mh2 = tmp;
  }

  thread writer1(writerThread1);
  thread writer2(writerThread2);

  while (ShouldContinueTest1 != 1)
    sched_yield();
  while (ShouldContinueTest2 != 1)
    sched_yield();

  const auto bitmap1 = mh1->bitmap().bits();
  const auto bitmap2 = mh2->bitmap().bits();
  const auto bitmap3 = mh3->bitmap().bits();
  const auto len = mh1->bitmap().byteCount();
  ASSERT_EQ(len, mh2->bitmap().byteCount());
  ASSERT_EQ(len, mh3->bitmap().byteCount());

  ASSERT_TRUE(mh1->isMeshingCandidate());
  ASSERT_TRUE(mh2->isMeshingCandidate());
  ASSERT_TRUE(mh3->isMeshingCandidate());

  // we have a clique
  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap1, bitmap2, len));
  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap2, bitmap3, len));
  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap1, bitmap3, len));

  {
    const internal::vector<MiniHeap *> candidates = gheap.meshingCandidatesLocked(mh1->sizeClass());
    ASSERT_EQ(candidates.size(), 3ULL);
    ASSERT_TRUE(std::find(candidates.begin(), candidates.end(), mh1) != candidates.end());
    ASSERT_TRUE(std::find(candidates.begin(), candidates.end(), mh2) != candidates.end());
    ASSERT_TRUE(std::find(candidates.begin(), candidates.end(), mh3) != candidates.end());
  }

  note("ABOUT TO MESH");
  if (!invert2) {
    gheap.meshLocked(mh1, mh2);
    gheap.meshLocked(mh1, mh3);
  } else {
    gheap.meshLocked(mh2, mh3);
    gheap.meshLocked(mh1, mh2);
  }
  note("DONE MESHING");

  // ensure the count of set bits looks right
  ASSERT_EQ(mh1->inUseCount(), 3UL);

  // check that our two allocated objects still look right
  ASSERT_STREQ(s1, v1);
  ASSERT_STREQ(s2, v2);
  ASSERT_STREQ(s3, v3);

  // get an aliased pointer to the second string by pointer arithmetic
  // on the first string
  char *t2 = s1 + (ObjCount - 1) * StrLen;
  ASSERT_STREQ(s2, t2);
  char *t3 = s1 + (3) * StrLen;
  ASSERT_STREQ(s3, t3);

  ShouldExit1 = 1;
  ShouldExit2 = 1;
  writer1.join();
  writer2.join();

  // modify the second string, ensure the modification shows up on
  // string 3 (would fail if the two miniheaps weren't meshed)
  s2[0] = 'b';
  ASSERT_EQ(t2[0], 'b');

  s3[0] = 'b';
  ASSERT_EQ(t3[0], 'b');

  ASSERT_EQ(mh1->getOff(gheap.arenaBegin(), s1), 0);
  ASSERT_EQ(mh1->getOff(gheap.arenaBegin(), s2), ObjCount - 1);
  ASSERT_EQ(mh1->getOff(gheap.arenaBegin(), s3), 3);

  {
    const internal::vector<MiniHeap *> candidates = gheap.meshingCandidatesLocked(mh1->sizeClass());
    ASSERT_EQ(candidates.size(), 1ULL);
    ASSERT_EQ(candidates[0], mh1);
  }

  // we need to attach the miniheap, otherwise
  ASSERT_TRUE(!mh1->isAttached());
  mh1->setAttached(gettid(), gheap.freelistFor(mh1->freelistId(), sizeClass));
  ASSERT_TRUE(mh1->isAttached());

  // now free the objects by going through the global heap -- it
  // should redirect both objects to the same miniheap
  gheap.free(s1);
  ASSERT_TRUE(!mh1->isEmpty());
  gheap.free(s2);
  ASSERT_TRUE(!mh1->isEmpty());
  gheap.free(s3);
  ASSERT_TRUE(mh1->isEmpty());

  note("ABOUT TO FREE");
  gheap.freeMiniheap(mh1);
  note("DONE FREE");

  note("ABOUT TO SCAVENGE");
  gheap.scavenge(true);
  note("DONE SCAVENGE");

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
}

TEST(TripleMeshTest, MeshAll) {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }
  meshTestConcurrentWrite(false, false);
  meshTestConcurrentWrite(false, true);
  meshTestConcurrentWrite(true, false);
  meshTestConcurrentWrite(true, true);
}
