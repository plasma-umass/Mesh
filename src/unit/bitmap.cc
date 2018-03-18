// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <cstdint>
#include <cstdlib>
#include <unordered_map>

#include "gtest/gtest.h"

#include "bitmap.h"

#include <heaplayers.h>

TEST(BitmapTest, SetGet) {
  const int NTRIALS = 1000;

  for (int n = 10; n < 10000; n *= 2) {
    mesh::internal::Bitmap b{static_cast<size_t>(n)};

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
          ASSERT_FALSE(b.isSet(i));
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
  mesh::internal::Bitmap b{1024};

  uint64_t i = b.setFirstEmpty();
  ASSERT_EQ(i, 0ULL);

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
  ASSERT_EQ(i, 0ULL);

  i = b.setFirstEmpty(4);
  ASSERT_EQ(i, 4ULL);

  i = b.setFirstEmpty(111);
  ASSERT_EQ(i, 111ULL);
}

TEST(BitmapTest, Iter) {
  mesh::internal::Bitmap b{512};

  b.tryToSet(0);
  b.tryToSet(200);
  b.tryToSet(500);

  std::unordered_map<size_t, bool> bits;

  ASSERT_EQ(bits.size(), 0ULL);

  size_t n = 0;
  for (auto const &off : b) {
    bits[off] = true;
    n++;
  }

  ASSERT_EQ(n, 3ULL);
  ASSERT_EQ(bits.size(), 3ULL);

  ASSERT_EQ(bits[0], true);
  ASSERT_EQ(bits[200], true);
  ASSERT_EQ(bits[500], true);

  ASSERT_EQ(bits[1], false);
}

TEST(BitmapTest, Iter2) {
  mesh::internal::Bitmap b{512};

  b.tryToSet(200);
  b.tryToSet(500);

  std::unordered_map<size_t, bool> bits;

  ASSERT_EQ(bits.size(), 0ULL);

  size_t n = 0;
  for (auto const &off : b) {
    bits[off] = true;
    n++;
  }

  ASSERT_EQ(n, 2ULL);
  ASSERT_EQ(bits.size(), 2ULL);

  ASSERT_EQ(bits[200], true);
  ASSERT_EQ(bits[500], true);

  ASSERT_EQ(bits.find(0), bits.end());
}

TEST(BitmapTest, SetHalf) {
  for (size_t i = 2; i <= 2048; i *= 2) {
    const auto nBits = i;

    mesh::internal::Bitmap bitmap{nBits};

    ASSERT_TRUE(bitmap.byteCount() >= nBits / 8);

    for (size_t i = 0; i < nBits / 2; i++) {
      bitmap.tryToSet(i);
      ASSERT_TRUE(bitmap.isSet(i));
      ASSERT_TRUE(bitmap.inUseCount() == i + 1);
    }

    ASSERT_TRUE(bitmap.isSet(0));

    ASSERT_TRUE(bitmap.inUseCount() == nBits / 2);
  }
}
