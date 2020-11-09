// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_RUNTIME_H
#define MESH_RUNTIME_H

#include <pthread.h>
#include <thread>
#include <algorithm>
#include <signal.h>  // for stack_t

#include "internal.h"

#include "real.h"

#include "global_heap.h"
#include "mmap_heap.h"

#include "heaplayers.h"
#include "mpsc_buffer.h"

namespace mesh {

// function passed to pthread_create
typedef void *(*PthreadFn)(void *);

typedef ring_buffer<internal::vector<Span> *> FreeRingVector;
typedef ring_buffer<internal::FreeCmd *> FreeCmdRingVector;

class Runtime {
private:
  DISALLOW_COPY_AND_ASSIGN(Runtime);

  // ensure we don't mistakenly create additional runtime instances
  explicit Runtime();

public:
  void lock();
  void unlock();

  inline GlobalHeap &heap() {
    return _heap;
  }

  void startBgThread();
  void startFreePhysThread();
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
  void installSegfaultHandler();

  void updatePid() {
    _pid = getpid();
  }

  pid_t pid() const {
    return _pid;
  }

  bool sendFreeCmd(internal::FreeCmd *fComand) {
    return _pagesFreeCmdBuffer->try_push(fComand);
  }

  void selfFlush() {
  }

  internal::FreeCmd *getReturnCmdFromBg() {
    return _pagesReturnCmdBuffer->pop();
  }

  void expandFlushSpans(internal::vector<Span> &spans, bool sorted) {
    internal::vector<Span> tmp;
    tmp.reserve(_flushSpans.size() + spans.size());

    if (!sorted) {
      std::sort(spans.begin(), spans.end());
    }

    std::merge(_flushSpans.begin(), _flushSpans.end(), spans.begin(), spans.end(), std::back_inserter(tmp));

    _flushSpans.swap(tmp);
  }

  internal::vector<Span> &getFlushSpans() {
    return _flushSpans;
  }

  bool freeThreadRunning() const {
    return _freeThreadRunning;
  }

protected:
  bool jobFreeCmd();
  void autoFlush();

private:
  // initialize our pointer to libc's pthread_create, etc.  This
  // happens lazily, as the dynamic linker's dlopen calls into malloc
  // for memory allocation, so if we try to do this in MeshHeaps's
  // constructor we deadlock before main even runs.
  void initThreads();

  static void segfaultHandler(int sig, siginfo_t *siginfo, void *context);

  static void *bgThread(void *arg);
  static void *bgFreePhysThread(void *arg);

  friend Runtime &runtime();

  mutex _mutex{};
  int _signalFd{-2};
  pid_t _pid{};
  GlobalHeap _heap{};

  FreeCmdRingVector *_pagesFreeCmdBuffer{nullptr};
  FreeCmdRingVector *_pagesReturnCmdBuffer{nullptr};
  internal::vector<Span> _flushSpans;
  bool _freeThreadRunning{false};
};

// get a reference to the Runtime singleton
inline Runtime &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime) + sizeof(double) - 1) / sizeof(double)];
  static Runtime *runtimePtr = new (buf) Runtime{};
  return *runtimePtr;
}

}  // namespace mesh

#endif  // MESH_RUNTIME_H
