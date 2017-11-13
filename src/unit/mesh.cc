// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdint.h>
#include <stdlib.h>

#include "gtest/gtest.h"

#include "internal.h"
#include "runtime.h"

using namespace mesh;

static constexpr size_t StrLen = 128;
static constexpr size_t ObjCount = 32;

// shows up in strace logs, but otherwise does nothing
static inline void note(const char *note) {
  (void)write(-1, note, strlen(note));
}

static void meshTest(bool invert) {
  GlobalHeap &gheap = runtime().heap();

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0);

  // allocate two miniheaps for the same object size from our global heap
  MiniHeap *mh1 = gheap.allocMiniheap(StrLen);
  MiniHeap *mh2 = gheap.allocMiniheap(StrLen);

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 2);

  // sanity checks
  ASSERT_TRUE(mh1 != mh2);
  ASSERT_EQ(mh1->maxCount(), mh2->maxCount());
  ASSERT_EQ(mh1->maxCount(), ObjCount);

  // allocate two c strings, one from each miniheap at different offsets
  char *s1 = reinterpret_cast<char *>(mh1->mallocAt(0));
  char *s2 = reinterpret_cast<char *>(mh2->mallocAt(ObjCount - 1));

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
  ASSERT_TRUE(strcmp(s1, v1) == 0);
  ASSERT_TRUE(strcmp(s2, v2) == 0);

  ASSERT_EQ(mh1->inUseCount(), 1);
  ASSERT_EQ(mh2->inUseCount(), 1);

  if (invert) {
    MiniHeap *tmp = mh1;
    mh1 = mh2;
    mh2 = tmp;
  }

  const auto bitmap1 = mh1->bitmap().bitmap();
  const auto bitmap2 = mh2->bitmap().bitmap();
  const auto len = mh1->bitmap().byteCount();
  ASSERT_EQ(len, mh2->bitmap().byteCount());

  ASSERT_TRUE(mesh::bitmapsMeshable(bitmap1, bitmap2, len));

  note("ABOUT TO MESH");
  // mesh the two miniheaps together
  gheap.meshLocked(mh1, mh2);
  note("DONE MESHING");

  // mh2 is consumed by mesh call, ensure it is now a null pointer
  ASSERT_EQ(mh2, nullptr);

  // ensure the count of set bits looks right
  ASSERT_EQ(mh1->inUseCount(), 2);

  // check that our two allocated objects still look right
  ASSERT_TRUE(strcmp(s1, v1) == 0);
  ASSERT_TRUE(strcmp(s2, v2) == 0);

  // get an aliased pointer to the second string by pointer arithmatic
  // on the first string
  char *s3 = s1 + (ObjCount - 1) * StrLen;
  ASSERT_TRUE(strcmp(s2, s3) == 0);

  // modify the second string, ensure the modification shows up on
  // string 3 (would fail if the two miniheaps weren't meshed)
  s2[0] = 'b';
  ASSERT_EQ(s3[0], 'b');

  // now free the objects by going through the global heap -- it
  // should redirect both objects to the same miniheap
  gheap.free(s1);
  ASSERT_TRUE(!mh1->isEmpty());
  gheap.free(s2);
  ASSERT_TRUE(mh1->isEmpty());  // safe because mh1 isn't "done"

  note("ABOUT TO FREE");
  gheap.freeMiniheap(mh1);

  ASSERT_EQ(gheap.getAllocatedMiniheapCount(), 0);
}

TEST(MeshTest, TryMesh) {
  meshTest(false);
}

TEST(MeshTest, TryMeshInverse) {
  meshTest(true);
}
