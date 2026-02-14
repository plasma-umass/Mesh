// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <dlfcn.h>

#include "real.h"

#include "common.h"

#define DEFINE_REAL(name) decltype(::name) *name

// Try to find a symbol via RTLD_NEXT first.  When that fails (e.g. glibc 2.34+
// merged libpthread into libc, or the runtime statically links pthread), fall
// back to explicitly opening the system libraries that are known to provide the
// symbol.  We cannot use RTLD_DEFAULT as a fallback because it would return
// Mesh's own wrapper and cause infinite recursion.
static void *fallback_dlsym(const char *symbol) {
  static const char *const libs[] = {
#ifdef __linux__
      "libpthread.so.0",
      "libc.so.6",
#elif defined(__APPLE__)
      "libSystem.B.dylib",
#endif
  };
  for (auto lib : libs) {
    void *h = dlopen(lib, RTLD_LAZY);
    if (h) {
      void *sym = dlsym(h, symbol);
      if (sym)
        return sym;
    }
  }
  return nullptr;
}

#define INIT_REAL(name, handle)                                         \
  do {                                                                  \
    name = reinterpret_cast<decltype(::name) *>(dlsym(handle, #name));  \
    if (name == nullptr)                                                \
      name = reinterpret_cast<decltype(::name) *>(fallback_dlsym(#name)); \
    hard_assert_msg(name != nullptr, "mesh::real: expected %s", #name); \
  } while (false)

namespace mesh {
namespace real {
#ifdef __linux__
DEFINE_REAL(epoll_pwait);
DEFINE_REAL(epoll_wait);
DEFINE_REAL(recv);
DEFINE_REAL(recvmsg);
#endif

DEFINE_REAL(pthread_create);
DEFINE_REAL(pthread_exit);

DEFINE_REAL(sigaction);
DEFINE_REAL(sigprocmask);

DEFINE_REAL(posix_spawn);
DEFINE_REAL(posix_spawnp);

void init() {
  static mutex initLock;
  static bool initialized;

  lock_guard<mutex> lock(initLock);
  if (initialized)
    return;
  initialized = true;
#ifdef __linux__
  INIT_REAL(epoll_pwait, RTLD_NEXT);
  INIT_REAL(epoll_wait, RTLD_NEXT);
  INIT_REAL(recv, RTLD_NEXT);
  INIT_REAL(recvmsg, RTLD_NEXT);
#endif

  INIT_REAL(pthread_create, RTLD_NEXT);
  INIT_REAL(pthread_exit, RTLD_NEXT);

  INIT_REAL(sigaction, RTLD_NEXT);
  INIT_REAL(sigprocmask, RTLD_NEXT);

  INIT_REAL(posix_spawn, RTLD_NEXT);
  INIT_REAL(posix_spawnp, RTLD_NEXT);
}
}  // namespace real
}  // namespace mesh
