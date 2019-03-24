// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <dlfcn.h>

#include "real.h"

#include "common.h"

#define DEFINE_REAL(name) decltype(::name) *name
#define INIT_REAL(name, handle)                                         \
  do {                                                                  \
    name = reinterpret_cast<decltype(::name) *>(dlsym(handle, #name));  \
    hard_assert_msg(name != nullptr, "mesh::real: expected %s", #name); \
  } while (false)

namespace mesh {
namespace real {
#ifdef __linux__
DEFINE_REAL(epoll_pwait);
DEFINE_REAL(epoll_wait);
#endif

DEFINE_REAL(pthread_create);
DEFINE_REAL(pthread_exit);

DEFINE_REAL(sigaction);
DEFINE_REAL(sigprocmask);

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
#endif

  INIT_REAL(pthread_create, RTLD_NEXT);
  INIT_REAL(pthread_exit, RTLD_NEXT);

  INIT_REAL(sigaction, RTLD_NEXT);
  INIT_REAL(sigprocmask, RTLD_NEXT);
}
}  // namespace real
}  // namespace mesh
