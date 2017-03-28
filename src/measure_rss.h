// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MEASURE_RSS_H
#define MESH__MEASURE_RSS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
  int npids;
  char *name;
  size_t rss;
  size_t dirty;
  float pss;
  float shared;
  float heap;
  float swap;
} CmdInfo;

int get_self_rss(CmdInfo *ci);
void print_self_rss(void);

#ifdef __cplusplus
}
#endif

#endif  // MESH__MEASURE_RSS_H
