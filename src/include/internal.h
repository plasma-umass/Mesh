// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_INTERNAL_H
#define MESH_INTERNAL_H

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

void debug(const char *fmt, ...);

#endif  // MESH_INTERNAL_H
