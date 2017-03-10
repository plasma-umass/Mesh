// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdint.h>
#include <stdlib.h>

#include "gtest/gtest.h"

#include "bitmap.h"

#include <heaplayers.h>

TEST(BitmapTest, SetGet) {
  const int NTRIALS = 1000;

  for (int n = 100; n < 10000; n *= 2) {
    mesh::Bitmap<MallocHeap> b{static_cast<size_t>(n)};

    for (int k = 0; k < NTRIALS; k++) {
      // Generate a random stream of bits.
      int *rnd = reinterpret_cast<int *>(calloc(n, sizeof(int)));
      ASSERT_NE(rnd, nullptr);

      for (int i = 0; i < n; i++) {
        rnd[i] = lrand48() % 2;
      }

      for (int i = 0; i < n; i++) {
        if (rnd[i] == 0) {
          bool r = b.tryToSet(i);
          ASSERT_TRUE(r);
        } else {
          b.unset(i);
        }
      }
      for (int i = 0; i < n; i++) {
        if (rnd[i] == 0) {
          ASSERT_TRUE(b.isSet(i));
          b.unset(i);
        } else {
          ASSERT_FALSE(b.isSet(i));
        }
      }
      free(rnd);
    }
  }
}

TEST(BitmapTest, Builtins) {
  mesh::Bitmap<MallocHeap> b{1024};

  uint64_t i = b.setFirstEmpty();
  ASSERT_EQ(i, 0);

  b.unset(i);

  static constexpr uint64_t curr = 66;
  for (size_t i = 0; i < curr; i++) {
    b.tryToSet(i);
  }

  i = b.setFirstEmpty();
  ASSERT_EQ(i, curr);

  for (size_t i = 0; i < curr; i++) {
    b.unset(i);
  }

  i = b.setFirstEmpty();
  ASSERT_EQ(i, 0);

  i = b.setFirstEmpty(4);
  ASSERT_EQ(i, 4);

  i = b.setFirstEmpty(111);
  ASSERT_EQ(i, 111);
}
