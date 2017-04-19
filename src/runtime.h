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

// function passed to pthread_create
typedef void *(*PthreadFn)(void *);

// signature of pthread_create itself
typedef int (*PthreadCreateFn)(pthread_t *thread, const pthread_attr_t *attr, PthreadFn start_routine, void *arg);

static const int NBins = 11;  // 16Kb max object size
static const int MeshPeriod = 1000;

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

class Runtime;

class StopTheWorld {
private:
  DISALLOW_COPY_AND_ASSIGN(StopTheWorld);

  // ensure we don't mistakenly create additional STW instances
  explicit StopTheWorld() {
  }

public:
  friend Runtime;

  void lock();
  void unlock();

private:
  void quiesceOthers();
  void quiesceSelf();
  void resume();

  mutex _sharedMu{};

  atomic_int _waiters{0};
  condition_variable _waitersCv{};

  atomic_int64_t _resumeEpoch{0};
  condition_variable _resumeCv{};
};

class Runtime {
private:
  DISALLOW_COPY_AND_ASSIGN(Runtime);

  // ensure we don't mistakenly create additional runtime instances
  explicit Runtime();

public:
  void lock();
  void unlock();

  inline StopTheWorld &stopTheWorld() {
    return _stw;
  }

  inline GlobalHeap &heap() {
    return _heap;
  }

  int createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg);

  static inline LocalHeap *localHeap() __attribute__((always_inline)) {
    if (unlikely(_localHeap == nullptr))
      allocLocalHeap();
    return _localHeap;
  }
  void registerThread(STWThreadState *stwState);

private:
  // initialize our pointer to libc's pthread_create, etc.  This
  // happens lazily, as the dynamic linker's dlopen calls into malloc
  // for memory allocation, so if we try to do this in MeshHeaps's
  // constructor we deadlock before main even runs.
  void initThreads();

  struct StartThreadArgs {
    explicit StartThreadArgs(Runtime *runtime_, PthreadFn startRoutine_, void *arg_)
        : runtime(runtime_), startRoutine(startRoutine_), arg(arg_) {
    }

    Runtime *runtime;
    PthreadFn startRoutine;
    void *arg;
  };

  static void *startThread(StartThreadArgs *threadArgs);
  static void sigQuiesceHandler(int sig, siginfo_t *info, void *uctx);

  void createSignalFd();
  void startBgThread();
  static void *bgThread(void *arg);

  void unregisterThread(STWThreadState *tc);

  void installSigHandlers();
  void installSigAltStack();
  void removeSigAltStack();

  void quiesceSelf();

  static void allocLocalHeap();

  // allocated inside LocalMeshingHeap, passed to runtime through registerThread
  static __thread STWThreadState *_stwState;
  static __thread stack_t _altStack;
  static __thread LocalHeap *_localHeap;

  friend Runtime &runtime();
  friend StopTheWorld;

  PthreadCreateFn _pthreadCreate{nullptr};
  GlobalHeap _heap{};
  mutex _mutex{};
  StopTheWorld _stw{};
  STWThreadState *_threads{nullptr};
  int _signalFd{-2};
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
