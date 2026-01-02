// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_PLATFORM_PLATFORM_H
#define MESH_PLATFORM_PLATFORM_H

// Platform detection
#if defined(_WIN32)
#define MESH_PLATFORM_WINDOWS 1
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Ensure min/max macros don't interfere
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#elif defined(__linux__)
#define MESH_PLATFORM_LINUX 1
#elif defined(__APPLE__)
#define MESH_PLATFORM_MACOS 1
#elif defined(__FreeBSD__)
#define MESH_PLATFORM_FREEBSD 1
#endif

// Unix-like platforms
#if defined(MESH_PLATFORM_LINUX) || defined(MESH_PLATFORM_MACOS) || defined(MESH_PLATFORM_FREEBSD)
#define MESH_PLATFORM_UNIX 1
#endif

namespace mesh {
namespace platform {

#if MESH_PLATFORM_WINDOWS
using FileHandle = HANDLE;
// INVALID_HANDLE_VALUE is not constexpr on MSVC, so use inline variable
inline FileHandle InvalidFileHandle = INVALID_HANDLE_VALUE;
#else
using FileHandle = int;
static constexpr FileHandle InvalidFileHandle = -1;
#endif

// Page protection constants (platform-independent)
enum Protection : int {
  kProtNone = 0,
  kProtRead = 1,
  kProtWrite = 2,
  kProtReadWrite = kProtRead | kProtWrite,
  kProtExec = 4,
  kProtReadExec = kProtRead | kProtExec,
  kProtReadWriteExec = kProtReadWrite | kProtExec
};

// Memory advice constants (platform-independent)
enum MemAdvice : int {
  kAdviceNormal = 0,
  kAdviceDontNeed = 1,
  kAdviceWillNeed = 2,
  kAdviceDontDump = 3,
  kAdviceDoDump = 4
};

}  // namespace platform
}  // namespace mesh

#endif  // MESH_PLATFORM_PLATFORM_H
