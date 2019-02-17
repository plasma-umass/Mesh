// -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef PLASMA__MESH_H
#define PLASMA__MESH_H

#include <stddef.h>

#define MESH_VERSION_MAJOR 1
#define MESH_VERSION_MINOR 0

#ifdef __cplusplus
extern "C" {
#endif

// Same API as je_mallctl, allows a program to query stats and set
// allocator-related options.
int mesh_mallctl(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

// 0 if not in bounds, 1 if is.
int mesh_in_bounds(void *ptr);

// returns the usable size of an allocation
size_t mesh_usable_size(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* PLASMA__MESH_H */
