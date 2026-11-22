// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_RUNTIME_H
#define MESH_RUNTIME_H

#include <pthread.h>
#include <signal.h>  // for stack_t

#ifdef __FreeBSD__
#include <pthread_np.h>
extern "C" {
static pid_t gettid(void) {
  return static_cast<pid_t>(pthread_getthreadid_np());
}
}
#endif

#include "internal.h"

#include "real.h"

#include "global_heap.h"
#include "mmap_heap.h"

#include "heaplayers.h"

namespace mesh {

// function passed to pthread_create
typedef void *(*PthreadFn)(void *);

template <size_t PageSize>
class Runtime {
private:
  DISALLOW_COPY_AND_ASSIGN(Runtime);

  // ensure we don't mistakenly create additional runtime instances
  explicit Runtime() {
    if (internal::getSeedMutex() == nullptr) {
      // force init
      internal::seed();
    }
    // a userfaultfd barrier that falls back to mprotect needs our handler
    _heap.setBarrierFallbackHook(&Runtime::barrierFallback);
  }

public:
  void lock();
  void unlock();

  inline GlobalHeap<PageSize> &heap() {
    return _heap;
  }

  void startBgThread();
  void initMaxMapCount();

  // we need to wrap pthread_create and pthread_exit so that we can
  // install our segfault handler and cleanup thread-local heaps.
  int createThread(pthread_t *thread, const pthread_attr_t *attr, mesh::PthreadFn startRoutine, void *arg);
  void ATTRIBUTE_NORETURN exitThread(void *retval);

  void setMeshPeriodMs(std::chrono::milliseconds period) {
    _heap.setMeshPeriodMs(period);
  }

#ifdef __linux__
  int epollWait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout);
  int epollPwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout, const __sigset_t *__ss);
  ssize_t recv(int sockfd, void *buf, size_t len, int flags);
  ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags);
#endif

  int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
  int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);

  struct StartThreadArgs {
    explicit StartThreadArgs(Runtime *runtime_, PthreadFn startRoutine_, void *arg_)
        : runtime(runtime_), startRoutine(startRoutine_), arg(arg_) {
    }

    Runtime *runtime;
    PthreadFn startRoutine;
    void *arg;
  };

  static void *startThread(StartThreadArgs *threadArgs);

  // so we can call from the libmesh init function
  void createSignalFd();
  // Installs Mesh's SIGSEGV/SIGBUS handler if the write barrier needs it
  // (always under mprotect; never under userfaultfd unless it fell back), or
  // unconditionally with force.  Idempotent.
  void installSegfaultHandler(bool force = false);

  bool segfaultHandlerInstalled() const {
    return _segfaultHandlerInstalled.load(std::memory_order_acquire);
  }

  void updatePid() {
    _pid = getpid();
  }

  pid_t pid() const {
    return _pid;
  }

private:
  // initialize our pointer to libc's pthread_create, etc.  This
  // happens lazily, as the dynamic linker's dlopen calls into malloc
  // for memory allocation, so if we try to do this in MeshHeaps's
  // constructor we deadlock before main even runs.
  void initThreads();

  static void segfaultHandler(int sig, siginfo_t *siginfo, void *context);
  static void barrierFallback();

  static void *bgThread(void *arg);

  template <size_t P>
  friend Runtime<P> &runtime();

  mutex _mutex{};
  int _signalFd{-2};
  // must be valid from the start: segfaultHandler compares it against
  // getpid() to detect a post-fork child.  (Previously this was only set by
  // that comparison's own side effect on the first meshing fault.)
  pid_t _pid{getpid()};
  atomic<bool> _segfaultHandlerInstalled{false};
  GlobalHeap<PageSize> _heap{};
};

// get a reference to the Runtime singleton
template <size_t PageSize>
inline Runtime<PageSize> &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime<PageSize>) + sizeof(double) - 1) / sizeof(double)];
  static Runtime<PageSize> *runtimePtr = new (buf) Runtime<PageSize>{};
  return *runtimePtr;
}
}  // namespace mesh

#include "global_heap_impl.h"

#endif  // MESH_RUNTIME_H