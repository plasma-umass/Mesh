// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2025 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <array>

#include "gtest/gtest.h"

#include "mesh_trigger.h"

using mesh::internal::MeshTrigger;

namespace {

constexpr std::array<uint64_t, 3> kBudgets{{100, 64, 32}};

TEST(MeshTriggerTest, RequestsAfterBudget) {
  MeshTrigger<3> trigger{kBudgets};
  size_t sc = 0;

  trigger.add(0, 40);
  EXPECT_FALSE(trigger.popRequested(-1, sc));

  trigger.add(0, 60);
  ASSERT_TRUE(trigger.popRequested(-1, sc));
  EXPECT_EQ(0u, sc);

  trigger.onMeshComplete(0, true);
  EXPECT_EQ(0u, trigger.backoff(0));
  EXPECT_EQ(0u, trigger.pendingBytes(0));
}

TEST(MeshTriggerTest, BackoffExpandsBudget) {
  MeshTrigger<3> trigger{kBudgets};
  size_t sc = 0;

  trigger.add(1, 64);
  ASSERT_TRUE(trigger.popRequested(-1, sc));
  EXPECT_EQ(1u, sc);
  trigger.onMeshComplete(1, false);

  EXPECT_EQ(1u, trigger.backoff(1));
  EXPECT_EQ(kBudgets[1] * 2, trigger.adjustedBudget(1));

  trigger.add(1, 60);
  EXPECT_FALSE(trigger.popRequested(-1, sc));
  trigger.add(1, 80);
  ASSERT_TRUE(trigger.popRequested(-1, sc));
  EXPECT_EQ(1u, sc);
}

TEST(MeshTriggerTest, PreferredPickRespectsMask) {
  MeshTrigger<3> trigger{kBudgets};
  size_t sc = 0;

  trigger.add(1, 100);
  trigger.add(2, 40);
  trigger.add(2, 40);

  ASSERT_TRUE(trigger.popRequested(2, sc));
  EXPECT_EQ(2u, sc);

  ASSERT_TRUE(trigger.popRequested(1, sc));
  EXPECT_EQ(1u, sc);
}

}  // namespace
