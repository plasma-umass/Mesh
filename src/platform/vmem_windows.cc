// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef _WIN32

#include "vmem.h"

#include <memoryapi.h>
#include <fileapi.h>
#include <winioctl.h>
#include <psapi.h>

namespace mesh {
namespace platform {

// Windows 10 1803+ APIs for page-granular file mapping
// These are loaded dynamically to maintain compatibility with older Windows versions.

// VirtualAlloc2 and MapViewOfFile3 function pointer types
typedef PVOID(WINAPI *VirtualAlloc2Func)(
    HANDLE Process,
    PVOID BaseAddress,
    SIZE_T Size,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount
);

typedef PVOID(WINAPI *MapViewOfFile3Func)(
    HANDLE FileMapping,
    HANDLE Process,
    PVOID BaseAddress,
    ULONG64 Offset,
    SIZE_T ViewSize,
    ULONG AllocationType,
    ULONG PageProtection,
    MEM_EXTENDED_PARAMETER *ExtendedParameters,
    ULONG ParameterCount
);

typedef PVOID(WINAPI *UnmapViewOfFile2Func)(
    HANDLE Process,
    PVOID BaseAddress,
    ULONG UnmapFlags
);

// Global function pointers - initialized lazily
static VirtualAlloc2Func pVirtualAlloc2 = nullptr;
static MapViewOfFile3Func pMapViewOfFile3 = nullptr;
static UnmapViewOfFile2Func pUnmapViewOfFile2 = nullptr;
static bool g_modernApisChecked = false;
static bool g_modernApisAvailable = false;

// Check if modern APIs are available and load them
static bool checkModernApis() {
  if (g_modernApisChecked) {
    return g_modernApisAvailable;
  }

  g_modernApisChecked = true;

  HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
  if (!kernelbase) {
    kernelbase = GetModuleHandleW(L"kernel32.dll");
  }

  if (kernelbase) {
    pVirtualAlloc2 = reinterpret_cast<VirtualAlloc2Func>(
        GetProcAddress(kernelbase, "VirtualAlloc2"));
    pMapViewOfFile3 = reinterpret_cast<MapViewOfFile3Func>(
        GetProcAddress(kernelbase, "MapViewOfFile3"));
    pUnmapViewOfFile2 = reinterpret_cast<UnmapViewOfFile2Func>(
        GetProcAddress(kernelbase, "UnmapViewOfFile2"));
  }

  g_modernApisAvailable = (pVirtualAlloc2 != nullptr && pMapViewOfFile3 != nullptr);
  return g_modernApisAvailable;
}

// Returns true if Windows 10 1803+ APIs are available for page-granular mapping
bool hasPageGranularMapping() {
  return checkModernApis();
}

// Reserve a contiguous address range as a placeholder (Windows 10 1803+)
// This allows us to later map file sections into sub-regions.
void *reservePlaceholder(size_t size) {
  if (!checkModernApis()) {
    // Fallback: use regular VirtualAlloc to reserve address space
    return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
  }

  // Use VirtualAlloc2 to create a placeholder region
  void *result = pVirtualAlloc2(
      GetCurrentProcess(),
      nullptr,                          // Let system choose address
      size,
      MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
      PAGE_NOACCESS,
      nullptr,
      0
  );

  return result;
}

// Split a placeholder into two parts (needed for mapping sub-regions)
bool splitPlaceholder(void *addr, size_t splitOffset) {
  if (!checkModernApis()) {
    return false;
  }

  // VirtualFree with MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER splits the placeholder
  return VirtualFree(addr, splitOffset, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) != 0;
}

// Map a file section into a placeholder region (modern API)
void *mapIntoPlaceholder(FileHandle handle, void *addr, size_t size, size_t offset) {
  if (!checkModernApis()) {
    return nullptr;
  }

  void *result = pMapViewOfFile3(
      handle,
      GetCurrentProcess(),
      addr,
      static_cast<ULONG64>(offset),
      size,
      MEM_REPLACE_PLACEHOLDER,
      PAGE_READWRITE,
      nullptr,
      0
  );

  return result;
}

FileHandle createSharedMemory(size_t size) {
  // Create a pagefile-backed section object (anonymous shared memory).
  // This is the Windows equivalent of memfd_create().
  //
  // NOTE: On Windows, the arena size is reduced (4GB vs 64GB on Linux) to
  // avoid pagefile commitment issues. SEC_COMMIT is used because SEC_RESERVE
  // with pagefile-backed sections can have issues with MapViewOfFile.
  HANDLE hSection = CreateFileMappingW(
      INVALID_HANDLE_VALUE,  // Use system pagefile as backing store
      NULL,                  // Default security attributes
      PAGE_READWRITE | SEC_COMMIT,
      static_cast<DWORD>(size >> 32),   // High 32 bits of size
      static_cast<DWORD>(size & 0xFFFFFFFF),  // Low 32 bits of size
      NULL                   // No name (anonymous section)
  );

  if (hSection == NULL) {
    // CreateFileMapping failed
    return InvalidFileHandle;
  }

  return hSection;
}

void closeSharedMemory(FileHandle handle) {
  if (handle != InvalidFileHandle && handle != NULL) {
    CloseHandle(handle);
  }
}

void *mapSharedFixed(FileHandle handle, void *addr, size_t size, size_t offset) {
  if (handle == InvalidFileHandle || handle == NULL) {
    return nullptr;
  }

  // Check if modern APIs are available (Windows 10 1803+)
  if (checkModernApis()) {
    // Use MapViewOfFile3 which supports page-granular offsets and placeholder replacement
    //
    // First, unmap the existing view. We use UnmapViewOfFile2 with MEM_PRESERVE_PLACEHOLDER
    // if it was a placeholder, otherwise regular UnmapViewOfFile.
    if (pUnmapViewOfFile2) {
      // Try to preserve as placeholder first (in case this was a placeholder region)
      pUnmapViewOfFile2(GetCurrentProcess(), addr, MEM_PRESERVE_PLACEHOLDER);
    } else {
      UnmapViewOfFile(addr);
    }

    // MapViewOfFile3 allows mapping at any address with page-granular offsets
    void *result = pMapViewOfFile3(
        handle,
        GetCurrentProcess(),
        addr,                              // Base address
        static_cast<ULONG64>(offset),      // Offset into section (page-granular!)
        size,
        MEM_REPLACE_PLACEHOLDER,           // Replace placeholder with mapping
        PAGE_READWRITE,
        nullptr,
        0
    );

    if (result == nullptr) {
      // If MEM_REPLACE_PLACEHOLDER failed, try without it (regular mapping)
      // This can happen if the region wasn't a placeholder
      result = pMapViewOfFile3(
          handle,
          GetCurrentProcess(),
          addr,
          static_cast<ULONG64>(offset),
          size,
          0,
          PAGE_READWRITE,
          nullptr,
          0
      );
    }

    if (result != addr && result != nullptr) {
      // Got a different address - unmap and fail
      UnmapViewOfFile(result);
      return nullptr;
    }

    return result;
  }

  // Fallback to legacy API (MapViewOfFileEx)
  // NOTE: This requires offset to be aligned to allocation granularity (64KB)!
  // Caller must ensure this for older Windows.
  size_t granularity = getAllocationGranularity();
  if ((offset % granularity) != 0) {
    // Offset not aligned - cannot use legacy API for page-granular mapping
    // Return nullptr to signal failure
    return nullptr;
  }

  // Unmap existing view
  UnmapViewOfFile(addr);

  // Now map the section at the specified address.
  void *result = MapViewOfFileEx(
      handle,
      FILE_MAP_ALL_ACCESS,
      static_cast<DWORD>(offset >> 32),
      static_cast<DWORD>(offset & 0xFFFFFFFF),
      size,
      addr
  );

  if (result != addr) {
    if (result != nullptr) {
      UnmapViewOfFile(result);
    }
    return nullptr;
  }

  return result;
}

void *mapShared(FileHandle handle, size_t size, Protection prot) {
  if (handle == InvalidFileHandle || handle == NULL) {
    return nullptr;
  }

  // Convert protection flags
  DWORD desiredAccess = 0;
  if ((prot & kProtRead) && (prot & kProtWrite)) {
    desiredAccess = FILE_MAP_ALL_ACCESS;
  } else if (prot & kProtRead) {
    desiredAccess = FILE_MAP_READ;
  } else if (prot & kProtWrite) {
    desiredAccess = FILE_MAP_WRITE;
  }

  void *result = MapViewOfFile(
      handle,
      desiredAccess,
      0,     // Offset high
      0,     // Offset low
      size   // Size (0 = map entire section)
  );

  return result;
}

bool unmap(void *addr, size_t size) {
  (void)size;  // Windows doesn't need size for unmapping
  return UnmapViewOfFile(addr) != 0;
}

bool protect(void *addr, size_t size, Protection prot) {
  DWORD newProtect = PAGE_NOACCESS;

  if ((prot & kProtRead) && (prot & kProtWrite) && (prot & kProtExec)) {
    newProtect = PAGE_EXECUTE_READWRITE;
  } else if ((prot & kProtRead) && (prot & kProtWrite)) {
    newProtect = PAGE_READWRITE;
  } else if ((prot & kProtRead) && (prot & kProtExec)) {
    newProtect = PAGE_EXECUTE_READ;
  } else if (prot & kProtRead) {
    newProtect = PAGE_READONLY;
  } else if (prot & kProtExec) {
    newProtect = PAGE_EXECUTE;
  }

  DWORD oldProtect;
  return VirtualProtect(addr, size, newProtect, &oldProtect) != 0;
}

bool decommit(void *addr, size_t size) {
  // Try DiscardVirtualMemory first (Windows 8.1+).
  // This is the closest equivalent to madvise(MADV_DONTNEED).
  typedef DWORD(WINAPI * DiscardVirtualMemoryFunc)(PVOID, SIZE_T);
  static DiscardVirtualMemoryFunc pDiscardVirtualMemory = nullptr;
  static bool checkedForDiscard = false;

  if (!checkedForDiscard) {
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32) {
      pDiscardVirtualMemory = reinterpret_cast<DiscardVirtualMemoryFunc>(
          GetProcAddress(kernel32, "DiscardVirtualMemory"));
    }
    checkedForDiscard = true;
  }

  if (pDiscardVirtualMemory != nullptr) {
    return pDiscardVirtualMemory(addr, size) == ERROR_SUCCESS;
  }

  // Fallback for older Windows: MEM_RESET hints to the OS that the pages
  // can be discarded. This is weaker than DiscardVirtualMemory but still helps.
  return VirtualAlloc(addr, size, MEM_RESET, PAGE_READWRITE) != nullptr;
}

bool punchHole(FileHandle handle, size_t offset, size_t size) {
  // On Windows, punching holes in pagefile-backed sections is limited.
  // For file-backed mappings, we could use FSCTL_SET_ZERO_DATA.
  // For pagefile-backed sections (our use case), the best we can do is
  // rely on decommit/DiscardVirtualMemory on the mapped view.
  //
  // Since mesh uses pagefile-backed sections (CreateFileMapping with
  // INVALID_HANDLE_VALUE), we can't directly punch holes in the backing store.
  // Instead, the calling code should use decommit() on the mapped addresses.

  (void)handle;
  (void)offset;
  (void)size;

  // Return true because we handle this differently on Windows.
  // The actual physical page release happens via decommit() on mapped views.
  return true;
}

void advise(void *addr, size_t size, MemAdvice advice) {
  switch (advice) {
    case kAdviceDontNeed:
      // Use decommit to release physical pages
      decommit(addr, size);
      break;
    case kAdviceWillNeed:
      // Prefetch pages - use PrefetchVirtualMemory if available (Win8+)
      {
        typedef BOOL(WINAPI * PrefetchVirtualMemoryFunc)(HANDLE, ULONG_PTR, PVOID, ULONG);
        static PrefetchVirtualMemoryFunc pPrefetch = nullptr;
        static bool checked = false;
        if (!checked) {
          HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
          if (kernel32) {
            pPrefetch = reinterpret_cast<PrefetchVirtualMemoryFunc>(
                GetProcAddress(kernel32, "PrefetchVirtualMemory"));
          }
          checked = true;
        }
        if (pPrefetch) {
          WIN32_MEMORY_RANGE_ENTRY entry;
          entry.VirtualAddress = addr;
          entry.NumberOfBytes = size;
          pPrefetch(GetCurrentProcess(), 1, &entry, 0);
        }
      }
      break;
    case kAdviceDontDump:
    case kAdviceDoDump:
      // Windows doesn't have a direct equivalent for core dump inclusion.
      // Minidump behavior is controlled differently.
      break;
    default:
      break;
  }
}

size_t getSystemPageSize() {
  static size_t pageSize = 0;
  if (pageSize == 0) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    pageSize = sysInfo.dwPageSize;
  }
  return pageSize;
}

size_t getAllocationGranularity() {
  static size_t granularity = 0;
  if (granularity == 0) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    granularity = sysInfo.dwAllocationGranularity;
  }
  return granularity;
}

}  // namespace platform
}  // namespace mesh

#endif  // _WIN32
