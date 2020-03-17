// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <cstdint>
#include <cstdlib>
#include <unordered_map>

#include "gtest/gtest.h"

#include "bitmap.h"

#include "heaplayers.h"

TEST(BitmapTest, RepresentationSize) {
  ASSERT_EQ(0UL, mesh::bitmap::representationSize(0));
  ASSERT_EQ(8UL, mesh::bitmap::representationSize(1));
  ASSERT_EQ(8UL, mesh::bitmap::representationSize(64));
  ASSERT_EQ(32UL, mesh::bitmap::representationSize(256));
  ASSERT_EQ(4UL, mesh::bitmap::representationSize(256) / sizeof(size_t));
}

TEST(BitmapTest, LowestSetBitAt) {
  mesh::internal::RelaxedBitmap bits{128};

  bits.tryToSet(6);
  ASSERT_EQ(6UL, bits.lowestSetBitAt(0));
  ASSERT_EQ(6UL, bits.lowestSetBitAt(5));
  ASSERT_EQ(6UL, bits.lowestSetBitAt(6));
  ASSERT_EQ(128UL, bits.lowestSetBitAt(7));
  bits.tryToSet(123);
  ASSERT_EQ(123UL, bits.lowestSetBitAt(7));
}

TEST(BitmapTest, HighestSetBitAt) {
  mesh::internal::RelaxedBitmap bits{128};

  bits.tryToSet(6);
  ASSERT_EQ(0UL, bits.highestSetBitBeforeOrAt(0));
  ASSERT_EQ(0UL, bits.highestSetBitBeforeOrAt(5));
  ASSERT_EQ(6UL, bits.highestSetBitBeforeOrAt(6));
  ASSERT_EQ(6UL, bits.highestSetBitBeforeOrAt(7));
  ASSERT_EQ(6UL, bits.highestSetBitBeforeOrAt(127));
  bits.tryToSet(123);
  ASSERT_EQ(123UL, bits.highestSetBitBeforeOrAt(127));
}

TEST(BitmapTest, SetAndExchangeAll) {
  const auto maxCount = 128;

  mesh::internal::Bitmap bitmap{maxCount};
  bitmap.tryToSet(3);
  bitmap.tryToSet(4);
  bitmap.tryToSet(127);

  mesh::internal::RelaxedFixedBitmap newBitmap{maxCount};
  newBitmap.setAll(maxCount);

  mesh::internal::RelaxedFixedBitmap localBits{maxCount};
  bitmap.setAndExchangeAll(localBits.mut_bits(), newBitmap.bits());
  localBits.invert();

  for (auto const &i : localBits) {
    if (i >= maxCount) {
      break;
    }
    ASSERT_TRUE(bitmap.isSet(i));
    ASSERT_TRUE(newBitmap.isSet(i));
    switch (i) {
    case 3:
    case 4:
    case 127:
      ASSERT_FALSE(localBits.isSet(i));
      break;
    default:
      ASSERT_TRUE(localBits.isSet(i));
      break;
    }
  }
}

TEST(BitmapTest, SetAll) {
  const auto maxCount = 88;

  uint64_t bits1[4] = {0, 0, 0, 0};
  mesh::internal::RelaxedBitmap bitmap1{maxCount, reinterpret_cast<char *>(bits1), false};
  for (size_t i = 0; i < maxCount; i++) {
    bitmap1.tryToSet(i);
  }

  uint64_t bits2[4] = {0, 0, 0, 0};
  mesh::internal::RelaxedBitmap bitmap2{maxCount, reinterpret_cast<char *>(bits2), false};
  bitmap2.setAll(maxCount);

  for (size_t i = 0; i < maxCount; i++) {
    ASSERT_TRUE(bitmap1.isSet(i));
    ASSERT_TRUE(bitmap2.isSet(i));
  }
}

TEST(BitmapTest, SetGet) {
  const int NTRIALS = 1000;

  for (int n = 2; n <= mesh::internal::Bitmap::MaxBitCount; n *= 2) {
    mesh::internal::Bitmap b{static_cast<size_t>(n)};
    int *rnd = reinterpret_cast<int *>(calloc(n, sizeof(int)));

    for (int k = 0; k < NTRIALS; k++) {
      // Generate a random stream of bits.
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
    }
    free(rnd);
  }
}

TEST(BitmapTest, SetGetRelaxed) {
  const int NTRIALS = 1000;

  for (int n = 10; n < 10000; n *= 2) {
    mesh::internal::RelaxedBitmap b{static_cast<size_t>(n)};
    int *rnd = reinterpret_cast<int *>(calloc(n, sizeof(int)));

    for (int k = 0; k < NTRIALS; k++) {
      // Generate a random stream of bits.
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
    }
    free(rnd);
  }
}

TEST(BitmapTest, Builtins) {
  mesh::internal::Bitmap b{256};

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
  mesh::internal::RelaxedBitmap b{512};

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
  mesh::internal::RelaxedBitmap b{512};

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

    mesh::internal::RelaxedBitmap bitmap{nBits};

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
