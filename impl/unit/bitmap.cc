// Copyright 2016 University of Massachusetts, Amherst

#include <stdint.h>
#include <stdlib.h>

#include "gtest/gtest.h"

#include "bitmap.h"

#include <heaplayers.h>

TEST(BitmapTest, SetGet) {
  const int NTRIALS = 1000;

  for (int n = 100; n < 10000; n *= 2) {
    Bitmap<MallocHeap> b;

    b.reserve(n);
    b.clear();

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
          b.reset(i);
        }
      }
      for (int i = 0; i < n; i++) {
        if (rnd[i] == 0) {
          ASSERT_TRUE(b.isSet(i));
          b.reset(i);
        } else {
          ASSERT_FALSE(b.isSet(i));
        }
      }
      free(rnd);
    }
  }
}
