// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <stdalign.h>
#include <cstdint>
#include <cstdlib>

#include "gtest/gtest.h"

#include "meshing.h"

using namespace mesh;

TEST(Cutoff, CreateTables) {
  for (size_t i = 8; i <= 256; i *= 2) {
    auto table = mesh::method::generateCutoffs(256, 0.80);
    mesh::debug("created table for %zu", i);
  }
  // for (size_t i = 0; i < 256; i++) {
  //   mesh::debug("\t%zu\t%zu", i, (size_t)table->get(i));
  // }
}
