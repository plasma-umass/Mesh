// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdint.h>
#include <stdlib.h>

#include "gtest/gtest.h"

#include "internal.h"
#include "meshing.h"
#include "runtime.h"

using namespace mesh;

static constexpr uint32_t StrLen = 128;
static constexpr uint32_t ObjCount = 32;

// shows up in strace logs, but otherwise does nothing
static inline void note(const char *note) {
  int _ __attribute__((unused)) = write(-1, note, strlen(note));
}

static void meshTest(bool invert) {
  if (!kMeshingEnabled) {
    GTEST_SKIP();
  }

  const auto tid = gettid();
  GlobalHeap &gheap = runtime().heap();

  // disable automatic meshing for this test
  gheap.setMeshPeriodMs(kZeroMs);

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);

  FixedArray<MiniHeap, 1> array{};

  // allocate two miniheaps for the same object size from our global heap
  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh1 = array[0];
  array.clear();

  gheap.allocSmallMiniheaps(SizeMap::SizeClass(StrLen), StrLen, array, tid);
  MiniHeap *mh2 = array[0];
  array.clear();

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 2UL);

  // sanity checks
  ASSERT_TRUE(mh1 != mh2);
  ASSERT_EQ(mh1->maxCount(), mh2->maxCount());
  ASSERT_EQ(mh1->maxCount(), ObjCount);

  ASSERT_EQ(mh1->bitmap().inUseCount(), 0UL);
  ASSERT_EQ(mh2->bitmap().inUseCount(), 0UL);

  // allocate two c strings, one from each miniheap at different offsets
  char *s1 = reinterpret_cast<char *>(mh1->mallocAt(gheap.arenaBegin(), 0));
  char *s2 = reinterpret_cast<char *>(mh2->mallocAt(gheap.arenaBegin(), ObjCount - 1));

  ASSERT_TRUE(s1 != nullptr);
  ASSERT_TRUE(s2 != nullptr);

  // fill in the strings, set the trailing null byte
  memset(s1, 'A', StrLen);
  memset(s2, 'Z', StrLen);
  s1[StrLen - 1] = 0;
  s2[StrLen - 1] = 0;

  // copy these strings so we can check the contents after meshing
  char *v1 = strdup(s1);
  char *v2 = strdup(s2);
  ASSERT_STREQ(s1, v1);
  ASSERT_STREQ(s2, v2);

  ASSERT_EQ(mh1->inUseCount(), 1UL);
  ASSERT_EQ(mh2->inUseCount(), 1UL);

  ASSERT_EQ(mh1->bitmap().inUseCount(), 1UL);
  ASSERT_EQ(mh2->bitmap().inUseCount(), 1UL);

  if (invert) {
    MiniHeap *tmp = mh1;
    mh1 = mh2;
    mh2 = tmp;
  }

  const auto bitmap1 = mh1->bitmap().bits();
  const auto bitmap2 = mh2->bitmap().bits();
  const auto len = mh1->bitmap().byteCount();
  ASSERT_EQ(len, mh2->bitmap().byteCount());

  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap1, bitmap2, len));

  note("ABOUT TO MESH");
  // mesh the two miniheaps together
  gheap.meshLocked(mh1, mh2);
  note("DONE MESHING");

  // ensure the count of set bits looks right
  ASSERT_EQ(mh1->inUseCount(), 2UL);

  // check that our two allocated objects still look right
  ASSERT_STREQ(s1, v1);
  ASSERT_STREQ(s2, v2);

  // get an aliased pointer to the second string by pointer arithmetic
  // on the first string
  char *s3 = s1 + (ObjCount - 1) * StrLen;
  ASSERT_STREQ(s2, s3);

  // modify the second string, ensure the modification shows up on
  // string 3 (would fail if the two miniheaps weren't meshed)
  s2[0] = 'b';
  ASSERT_EQ(s3[0], 'b');

  ASSERT_EQ(mh1->meshCount(), 2ULL);

  // now free the objects by going through the global heap -- it
  // should redirect both objects to the same miniheap
  gheap.free(s1);
  ASSERT_TRUE(!mh1->isEmpty());
  gheap.free(s2);
  ASSERT_TRUE(mh1->isEmpty());  // safe because mh1 isn't "done"

  note("ABOUT TO FREE");
  gheap.freeMiniheap(mh1);
  note("DONE FREE");

  note("ABOUT TO SCAVENGE");
  gheap.scavenge(true);
  note("DONE SCAVENGE");

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0UL);
}

TEST(MeshTest, TryMesh) {
  meshTest(false);
}

TEST(MeshTest, TryMeshInverse) {
  meshTest(true);
}
