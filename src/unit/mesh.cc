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

TEST(MeshTest, TryMesh) {
  GlobalHeap &gheap = runtime().heap();

  MiniHeap *mh1 = gheap.allocMiniheap(StrLen);
  MiniHeap *mh2 = gheap.allocMiniheap(StrLen);

  ASSERT_TRUE(mh1 != mh2);

  ASSERT_EQ(mh1->maxCount(), mh2->maxCount());
  ASSERT_EQ(mh1->maxCount(), ObjCount);

  char *s1 = reinterpret_cast<char *>(mh1->mallocAt(0));
  char *s2 = reinterpret_cast<char *>(mh2->mallocAt(ObjCount - 1));

  ASSERT_TRUE(s1 != nullptr);
  ASSERT_TRUE(s2 != nullptr);

  memset(s1, 'A', StrLen);
  memset(s2, 'Z', StrLen);

  s1[StrLen - 1] = 0;
  s2[StrLen - 1] = 0;

  char *v1 = strdup(s1);
  char *v2 = strdup(s2);

  ASSERT_TRUE(strcmp(s1, v1) == 0);
  ASSERT_TRUE(strcmp(s2, v2) == 0);

  gheap.mesh(mh1, mh2);

  // mh2 is consumed by mesh call
  ASSERT_EQ(mh2, nullptr);

  ASSERT_TRUE(strcmp(s1, v1) == 0);
  ASSERT_TRUE(strcmp(s2, v2) == 0);

  char *s3 = s1 + (ObjCount - 1) * StrLen;

  ASSERT_TRUE(strcmp(s2, s3) == 0);

  s2[0] = 'b';
  ASSERT_EQ(s3[0], 'b');

  gheap.free(s1);
  ASSERT_TRUE(!mh1->isEmpty());

  gheap.free(s2);
  // only safe because mh1 isn't "done"
  ASSERT_TRUE(mh1->isEmpty());

  write(-1, "ABOUT TO FREE\n", strlen("ABOUT TO FREE\n"));
  gheap.freeMiniheap(mh1);
}
