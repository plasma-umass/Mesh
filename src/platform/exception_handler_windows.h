// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_PLATFORM_EXCEPTION_HANDLER_WINDOWS_H
#define MESH_PLATFORM_EXCEPTION_HANDLER_WINDOWS_H

#ifdef _WIN32

#include <windows.h>

namespace mesh {
namespace platform {

/// Install the Vectored Exception Handler for mesh write barriers.
/// This should be called once during library initialization.
void installExceptionHandler();

/// Remove the Vectored Exception Handler.
/// This should be called during library cleanup.
void removeExceptionHandler();

/// Check if the exception handler is installed.
bool isExceptionHandlerInstalled();

}  // namespace platform
}  // namespace mesh

#endif  // _WIN32

#endif  // MESH_PLATFORM_EXCEPTION_HANDLER_WINDOWS_H
