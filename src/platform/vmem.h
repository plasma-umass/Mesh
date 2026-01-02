// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_PLATFORM_VMEM_H
#define MESH_PLATFORM_VMEM_H

#include "platform.h"

#if MESH_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/auxv.h>
#endif
#endif

#include <cstddef>

namespace mesh {
namespace platform {

/// Create a shared memory object capable of being mapped at multiple virtual addresses.
/// This is the Windows equivalent of memfd_create() or creating a file in /dev/shm.
/// @param size The size of the shared memory region
/// @return A file handle to the shared memory, or InvalidFileHandle on failure
FileHandle createSharedMemory(size_t size);

/// Close a shared memory handle created by createSharedMemory.
/// @param handle The handle to close
void closeSharedMemory(FileHandle handle);

/// Map shared memory at a specific address.
/// Equivalent to mmap() with MAP_SHARED | MAP_FIXED.
/// @param handle The shared memory handle
/// @param addr The target virtual address (must be unmapped first on Windows)
/// @param size Size of the mapping
/// @param offset Offset into the shared memory object
/// @return The mapped address (should equal addr), or nullptr on failure
void *mapSharedFixed(FileHandle handle, void *addr, size_t size, size_t offset);

/// Map shared memory at any available address.
/// Equivalent to mmap() with MAP_SHARED (no MAP_FIXED).
/// @param handle The shared memory handle
/// @param size Size of the mapping
/// @param prot Protection flags (kProtRead, kProtWrite, etc.)
/// @return The mapped address, or nullptr on failure
void *mapShared(FileHandle handle, size_t size, Protection prot);

/// Unmap a memory region.
/// Equivalent to munmap().
/// @param addr The address to unmap
/// @param size Size of the region
/// @return true on success
bool unmap(void *addr, size_t size);

/// Change memory protection on a region.
/// Equivalent to mprotect().
/// @param addr The address to protect
/// @param size Size of the region
/// @param prot New protection flags
/// @return true on success
bool protect(void *addr, size_t size, Protection prot);

/// Release physical pages back to the OS without unmapping.
/// Equivalent to madvise(MADV_DONTNEED).
/// @param addr The address to decommit
/// @param size Size of the region
/// @return true on success
bool decommit(void *addr, size_t size);

/// Punch a hole in file-backed memory, releasing physical storage.
/// Equivalent to fallocate(FALLOC_FL_PUNCH_HOLE) on Linux.
/// @param handle The file handle
/// @param offset Offset into the file
/// @param size Size of the hole
/// @return true on success
bool punchHole(FileHandle handle, size_t offset, size_t size);

/// Provide advice about memory usage patterns.
/// Equivalent to madvise().
/// @param addr The address
/// @param size Size of the region
/// @param advice The advice to give
void advise(void *addr, size_t size, MemAdvice advice);

/// Get the system page size.
/// @return Page size in bytes
size_t getSystemPageSize();

/// Get the system allocation granularity.
/// On Unix this equals page size. On Windows it's typically 64KB.
/// @return Allocation granularity in bytes
size_t getAllocationGranularity();

/// Check if the platform supports page-granular file mapping.
/// On Windows, this returns true if VirtualAlloc2/MapViewOfFile3 are available (Win10 1803+).
/// On Unix, always returns true.
/// @return true if page-granular mapping is supported
bool hasPageGranularMapping();

/// Reserve a placeholder region that can later have file views mapped into it.
/// This is primarily for Windows 10 1803+ placeholder support.
/// @param size Size of the region to reserve
/// @return The reserved address, or nullptr on failure
void *reservePlaceholder(size_t size);

/// Split a placeholder region at the specified offset.
/// Only meaningful on Windows 10 1803+ with placeholder support.
/// @param addr Start of the placeholder region
/// @param splitOffset Offset at which to split
/// @return true on success
bool splitPlaceholder(void *addr, size_t splitOffset);

/// Map a file section into a placeholder region.
/// Only meaningful on Windows 10 1803+ with placeholder support.
/// @param handle The file mapping handle
/// @param addr The placeholder address to map into
/// @param size Size of the mapping
/// @param offset Offset into the file
/// @return The mapped address, or nullptr on failure
void *mapIntoPlaceholder(FileHandle handle, void *addr, size_t size, size_t offset);

// ============================================================================
// Platform-specific implementations (inline for Unix, separate file for Windows)
// ============================================================================

#if MESH_PLATFORM_UNIX

inline FileHandle createSharedMemory(size_t size) {
  // This is a stub - the actual implementation using memfd_create or /dev/shm
  // files remains in meshable_arena.h for Unix platforms to avoid changing
  // existing behavior.
  (void)size;
  return InvalidFileHandle;
}

inline void closeSharedMemory(FileHandle handle) {
  if (handle != InvalidFileHandle) {
    close(handle);
  }
}

inline void *mapSharedFixed(FileHandle handle, void *addr, size_t size, size_t offset) {
  void *result = mmap(addr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, handle, offset);
  return (result == MAP_FAILED) ? nullptr : result;
}

inline void *mapShared(FileHandle handle, size_t size, Protection prot) {
  int mprot = PROT_NONE;
  if (prot & kProtRead) mprot |= PROT_READ;
  if (prot & kProtWrite) mprot |= PROT_WRITE;
  if (prot & kProtExec) mprot |= PROT_EXEC;

  void *result = mmap(nullptr, size, mprot, MAP_SHARED, handle, 0);
  return (result == MAP_FAILED) ? nullptr : result;
}

inline bool unmap(void *addr, size_t size) {
  return munmap(addr, size) == 0;
}

inline bool protect(void *addr, size_t size, Protection prot) {
  int mprot = PROT_NONE;
  if (prot & kProtRead) mprot |= PROT_READ;
  if (prot & kProtWrite) mprot |= PROT_WRITE;
  if (prot & kProtExec) mprot |= PROT_EXEC;

  return mprotect(addr, size, mprot) == 0;
}

inline bool decommit(void *addr, size_t size) {
#ifdef MADV_DONTNEED
  return madvise(addr, size, MADV_DONTNEED) == 0;
#else
  (void)addr;
  (void)size;
  return true;
#endif
}

inline bool punchHole(FileHandle handle, size_t offset, size_t size) {
  // Stub - actual implementation remains platform-specific in meshable_arena.h
  (void)handle;
  (void)offset;
  (void)size;
  return true;
}

inline void advise(void *addr, size_t size, MemAdvice advice) {
#ifdef __linux__
  int madviceFlag = MADV_NORMAL;
  switch (advice) {
    case kAdviceDontNeed:
      madviceFlag = MADV_DONTNEED;
      break;
    case kAdviceWillNeed:
      madviceFlag = MADV_WILLNEED;
      break;
#ifdef MADV_DONTDUMP
    case kAdviceDontDump:
      madviceFlag = MADV_DONTDUMP;
      break;
    case kAdviceDoDump:
      madviceFlag = MADV_DODUMP;
      break;
#endif
    default:
      break;
  }
  madvise(addr, size, madviceFlag);
#elif defined(__APPLE__) || defined(__FreeBSD__)
  int madviceFlag = MADV_NORMAL;
  switch (advice) {
    case kAdviceDontNeed:
      madviceFlag = MADV_DONTNEED;
      break;
    case kAdviceWillNeed:
      madviceFlag = MADV_WILLNEED;
      break;
#ifdef MADV_NOCORE
    case kAdviceDontDump:
      madviceFlag = MADV_NOCORE;
      break;
    case kAdviceDoDump:
      madviceFlag = MADV_CORE;
      break;
#endif
    default:
      break;
  }
  madvise(addr, size, madviceFlag);
#else
  (void)addr;
  (void)size;
  (void)advice;
#endif
}

inline size_t getSystemPageSize() {
#ifdef __linux__
  // Use getauxval on Linux for ARM64 where page size may vary
  static size_t pageSize = getauxval(AT_PAGESZ);
  return pageSize;
#else
  static size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  return pageSize;
#endif
}

inline size_t getAllocationGranularity() {
  // On Unix, allocation granularity equals page size
  return getSystemPageSize();
}

inline bool hasPageGranularMapping() {
  // Unix always supports page-granular mmap
  return true;
}

inline void *reservePlaceholder(size_t size) {
  // On Unix, we can just use mmap with PROT_NONE to reserve address space
  void *result = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (result == MAP_FAILED) ? nullptr : result;
}

inline bool splitPlaceholder(void *addr, size_t splitOffset) {
  // On Unix, no need to split - mmap with MAP_FIXED can map into any part
  (void)addr;
  (void)splitOffset;
  return true;
}

inline void *mapIntoPlaceholder(FileHandle handle, void *addr, size_t size, size_t offset) {
  // On Unix, this is just mmap with MAP_FIXED
  void *result = mmap(addr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, handle, offset);
  return (result == MAP_FAILED) ? nullptr : result;
}

#endif  // MESH_PLATFORM_UNIX

// Windows implementations are in vmem_windows.cc

}  // namespace platform
}  // namespace mesh

#endif  // MESH_PLATFORM_VMEM_H
