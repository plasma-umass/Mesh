// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__RUNTIME_H
#define MESH__RUNTIME_H

#include <pthread.h>
#include <signal.h>  // for stack_t

#include <condition_variable>

#include "internal.h"

#include "globalmeshingheap.h"
#include "localmeshingheap.h"
#include "lockedheap.h"
#include "mmapheap.h"
#include "semiansiheap.h"

#include "heaplayers.h"

using std::condition_variable;

namespace mesh {

//static const int NBins = 11;  // 16Kb max object size
static const int NBins = 25;  // 16Kb max object size
static const int MeshPeriod = 10000;

typedef int (*EpollWaitFn)(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout);
typedef int (*EpollPwaitFn)(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout,
                            const __sigset_t *__ss);

typedef int (*SigActionFn)(int signum, const struct sigaction *act, struct sigaction *oldact);
typedef int (*SigProcMaskFn)(int how, const sigset_t *set, sigset_t *oldset);

// The global heap manages the spans that back MiniHeaps as well as
// large allocations.
class GlobalHeap : public GlobalMeshingHeap<mesh::MmapHeap, NBins, mesh::size2Class, mesh::class2Size, MeshPeriod> {};

// Fewer buckets than regular KingsleyHeap (to ensure multiple objects
// fit in the 128Kb spans used by MiniHeaps).
class LocalHeap
    : public SemiANSIHeap<LocalMeshingHeap<NBins, mesh::size2Class, mesh::class2Size, MeshPeriod, GlobalHeap>,
                          GlobalHeap> {
private:
  DISALLOW_COPY_AND_ASSIGN(LocalHeap);
  typedef SemiANSIHeap<LocalMeshingHeap<NBins, mesh::size2Class, mesh::class2Size, MeshPeriod, GlobalHeap>, GlobalHeap>
      SuperHeap;

public:
  explicit LocalHeap(GlobalHeap *global) : SuperHeap(global) {
  }
};

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

  static inline LocalHeap *localHeap() __attribute__((always_inline)) {
    if (unlikely(_localHeap == nullptr))
      allocLocalHeap();
    return _localHeap;
  }
  void startBgThread();
  void setMeshPeriodSecs(double period) {
    _heap.setMeshPeriodSecs(period);
  }

  int epollWait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout);
  int epollPwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout, const __sigset_t *__ss);
  int sigAction(int signum, const struct sigaction *act, struct sigaction *oldact);
  int sigProcMask(int how, const sigset_t *set, sigset_t *oldset);

  void initInterposition();
private:
  // initialize our pointer to libc's pthread_create, etc.  This
  // happens lazily, as the dynamic linker's dlopen calls into malloc
  // for memory allocation, so if we try to do this in MeshHeaps's
  // constructor we deadlock before main even runs.
  void initThreads();

  void createSignalFd();
  static void *bgThread(void *arg);

  static void allocLocalHeap();

  static __thread LocalHeap *_localHeap;

  friend Runtime &runtime();

  GlobalHeap _heap{};
  mutex _mutex{};
  int _signalFd{-2};

  EpollWaitFn _libcEpollWait{nullptr};
  EpollPwaitFn _libcEpollPwait{nullptr};
  SigActionFn _libcSigAction{nullptr};
  SigProcMaskFn _libcSigProcMask{nullptr};
};

// get a reference to the Runtime singleton
inline Runtime &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime) + sizeof(double) - 1) / sizeof(double)];
  static Runtime *runtimePtr = new (buf) Runtime{};
  return *runtimePtr;
}
}  // namespace mesh

#endif  // MESH__RUNTIME_H
