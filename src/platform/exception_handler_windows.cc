// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#ifdef _WIN32

#include "exception_handler_windows.h"

#include <windows.h>

#include "../common.h"
#include "../runtime.h"
#include "../dispatch_utils.h"

namespace mesh {
namespace platform {

// Forward declaration for checking if address is in mesh arena
template <size_t PageSize>
bool checkMeshFaultAddress(void *addr);

// Handle to the installed VEH
static PVOID g_vehHandle = nullptr;

/// Vectored Exception Handler for mesh write barriers.
/// This handler intercepts access violations during meshing operations.
/// When a page is being meshed, it's marked read-only. If the application
/// tries to write to it, we catch the exception, wait for meshing to complete,
/// and then allow the write to proceed.
static LONG CALLBACK MeshVectoredHandler(PEXCEPTION_POINTERS exInfo) {
  // Only handle access violations
  if (exInfo->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // ExceptionInformation[0]: 0 = read, 1 = write, 8 = DEP violation
  // ExceptionInformation[1]: address that was accessed
  ULONG_PTR accessType = exInfo->ExceptionRecord->ExceptionInformation[0];
  void *faultAddr = reinterpret_cast<void *>(exInfo->ExceptionRecord->ExceptionInformation[1]);

  // We only care about write faults (accessType == 1)
  if (accessType != 1) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // Check if the fault address is in our mesh arena and if it's safe to proceed
  // This uses the same logic as the Unix signal handler - checking okToProceed
  bool okToProceed = false;

  try {
    okToProceed = dispatchByPageSize([faultAddr](auto &rt) -> bool {
      return rt.heap().okToProceed(faultAddr);
    });
  } catch (...) {
    // If we can't check, it's not our fault
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (okToProceed) {
    // The meshing operation has completed and the page protection has been
    // restored. We can now retry the faulting instruction.
    return EXCEPTION_CONTINUE_EXECUTION;
  }

  // This isn't a mesh-related fault, let other handlers process it
  return EXCEPTION_CONTINUE_SEARCH;
}

void installExceptionHandler() {
  if (g_vehHandle != nullptr) {
    // Already installed
    return;
  }

  // Install as first handler (priority 1) so we see exceptions before
  // other handlers like the debugger
  g_vehHandle = AddVectoredExceptionHandler(1, MeshVectoredHandler);

  if (g_vehHandle == nullptr) {
    debug("mesh: failed to install VEH: %lu\n", GetLastError());
  }
}

void removeExceptionHandler() {
  if (g_vehHandle != nullptr) {
    RemoveVectoredExceptionHandler(g_vehHandle);
    g_vehHandle = nullptr;
  }
}

bool isExceptionHandlerInstalled() {
  return g_vehHandle != nullptr;
}

}  // namespace platform
}  // namespace mesh

#endif  // _WIN32
