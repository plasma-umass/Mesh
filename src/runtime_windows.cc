// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Windows-specific runtime implementation

#if defined(_WIN32)

#include <windows.h>
#include <psapi.h>

#include "runtime.h"
#include "runtime_impl.h"
#include "thread_local_heap.h"

namespace mesh {

ATTRIBUTE_ALIGNED(CACHELINE_SIZE)
const unsigned char SizeMap::class_array_[kClassArraySize] = {
#include "size_classes.def"
};

ATTRIBUTE_ALIGNED(CACHELINE_SIZE)
const int32_t SizeMap::class_to_size_[kClassSizesMax] = {
    16,  16,  32,  48,  64,  80,  96,  112,  128,  160,  192,  224,   256,
    320, 384, 448, 512, 640, 768, 896, 1024, 2048, 4096, 8192, 16384,
};

STLAllocator<char, internal::Heap> internal::allocator{};

// Windows doesn't have PSS (Proportional Set Size) - return working set instead
size_t internal::measurePssKiB() {
  PROCESS_MEMORY_COUNTERS pmc;
  pmc.cb = sizeof(pmc);
  if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    return 0;
  }
  // Return working set in KiB
  return static_cast<size_t>(pmc.WorkingSetSize / 1024);
}

// copyFile is not needed on Windows (no fork/exec scenarios)
// But we need to provide a stub for the declaration in internal.h
int internal::copyFile(int dstFd, int srcFd, off_t off, size_t sz) {
  // Not implemented on Windows
  (void)dstFd;
  (void)srcFd;
  (void)off;
  (void)sz;
  return -1;
}

// Windows background mesh thread implementation
template <size_t PageSize>
void Runtime<PageSize>::startBgThread() {
  if (_meshThreadStarted.exchange(true)) {
    // Already started
    return;
  }

  // Only start mesh thread if meshing is enabled
  if (!kMeshingEnabled) {
    return;
  }

  _meshThreadShutdown.store(false);

  _meshThread = std::thread([this]() {
    // Set thread name for debugging (Windows 10 1607+)
    typedef HRESULT(WINAPI * SetThreadDescriptionFunc)(HANDLE, PCWSTR);
    static SetThreadDescriptionFunc pSetThreadDescription = nullptr;
    static bool checked = false;
    if (!checked) {
      HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
      if (kernel32) {
        pSetThreadDescription = reinterpret_cast<SetThreadDescriptionFunc>(
            GetProcAddress(kernel32, "SetThreadDescription"));
      }
      checked = true;
    }
    if (pSetThreadDescription) {
      pSetThreadDescription(GetCurrentThread(), L"MeshBgThread");
    }

    // Default mesh period is 100ms (matching Unix default)
    constexpr auto meshPeriod = std::chrono::milliseconds(100);

    while (!_meshThreadShutdown.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(meshPeriod);

      if (!_meshThreadShutdown.load(std::memory_order_acquire)) {
        // Try to trigger meshing
        _heap.maybeMesh();
      }
    }
  });
}

template <size_t PageSize>
void Runtime<PageSize>::stopBgThread() {
  if (!_meshThreadStarted.load()) {
    return;
  }

  _meshThreadShutdown.store(true, std::memory_order_release);

  if (_meshThread.joinable()) {
    _meshThread.join();
  }

  _meshThreadStarted.store(false);
}

// Explicit instantiation of Runtime
template class Runtime<4096>;
template class Runtime<16384>;

}  // namespace mesh

#endif  // _WIN32
