// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Debug printf that doesn't call malloc - safe to use inside allocator

#pragma once
#ifndef MESH_DEBUG_PRINTF_H
#define MESH_DEBUG_PRINTF_H

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare fctprintf from printf.c - do NOT include printf.h as it
// redefines printf to printf_ which would affect all code including this header
int fctprintf(void (*out)(char character, void* arg), void* arg, const char* format, ...);

// Output function for fctprintf - writes directly to stderr
static inline void mesh_debug_putchar(char c, void* arg) {
  (void)arg;
#ifdef _WIN32
  HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
  DWORD written;
  WriteFile(hStderr, &c, 1, &written, NULL);
#else
  ssize_t ret = write(STDERR_FILENO, &c, 1);
  (void)ret;
#endif
}

#ifdef __cplusplus
}
#endif

// Debug printf macro - writes to stderr without malloc
// Set MESH_DEBUG_VERBOSE=1 to enable debug output
#ifdef MESH_DEBUG_VERBOSE
#define mesh_dprintf(...) fctprintf(mesh_debug_putchar, NULL, __VA_ARGS__)
#else
#define mesh_dprintf(...) ((void)0)
#endif

#endif  // MESH_DEBUG_PRINTF_H
