// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__RUNTIME_H
#define MESH__RUNTIME_H

#include <pthread.h>
#include <signal.h>  // for stack_t

#include "internal.h"

#include "globalmeshingheap.h"
#include "localmeshingheap.h"
#include "lockedheap.h"
#include "mmapheap.h"

#include "heaplayers.h"

using std::condition_variable;

namespace mesh {

// function passed to pthread_create
typedef void *(*PthreadFn)(void *);

// signature of pthread_create itself
typedef int (*PthreadCreateFn)(pthread_t *thread, const pthread_attr_t *attr, PthreadFn start_routine, void *arg);

static const int N_BINS = 12;  // 16Kb max object size
static const int MESH_PERIOD = 100;

// The global heap manages the spans that back MiniHeaps as well as
// large allocations.
class GlobalHeap
    : public mesh::LockedHeap<
          mesh::GlobalMeshingHeap<mesh::MmapHeap, N_BINS, mesh::size2Class, mesh::class2Size, MESH_PERIOD>> {};

// Fewer buckets than regular KingsleyHeap (to ensure multiple objects
// fit in the 128Kb spans used by MiniHeaps).
class LocalHeap : public mesh::LocalMeshingHeap<N_BINS, mesh::size2Class, mesh::class2Size, MESH_PERIOD, GlobalHeap> {
private:
  DISALLOW_COPY_AND_ASSIGN(LocalHeap);
  typedef mesh::LocalMeshingHeap<N_BINS, mesh::size2Class, mesh::class2Size, MESH_PERIOD, GlobalHeap> SuperHeap;

public:
  explicit LocalHeap(GlobalHeap *global) : SuperHeap(global) {
  }

  // from ANSIHeap
  inline void *calloc(size_t s1, size_t s2) {
    auto *ptr = (char *)malloc(s1 * s2);
    if (ptr) {
      memset(ptr, 0, s1 * s2);
    }
    return (void *)ptr;
  }

  // from ANSIHeap
  inline void *realloc(void *ptr, const size_t sz) {
    if (ptr == 0) {
      return malloc(sz);
    }
    if (sz == 0) {
      free(ptr);
      return 0;
    }

    auto objSize = getSize(ptr);
    if (objSize == sz) {
      return ptr;
    }

    // Allocate a new block of size sz.
    auto *buf = malloc(sz);

    // Copy the contents of the original object
    // up to the size of the new block.

    auto minSize = (objSize < sz) ? objSize : sz;
    if (buf) {
      memcpy(buf, ptr, minSize);
    }

    // Free the old block.
    free(ptr);
    return buf;
  }
};

// forward declaration of runtime so we can declare it a friend
class Runtime;
class StopTheWorld;

class ThreadCache {
private:
  DISALLOW_COPY_AND_ASSIGN(ThreadCache);

public:
  explicit ThreadCache();

private:
  friend Runtime;
  friend StopTheWorld;

  atomic_bool _waiting{false};
  atomic_int64_t _shutdownEpoch{-1};
  pthread_t _tid;
  ThreadCache *_prev{nullptr};
  ThreadCache *_next{nullptr};
};

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

  void registerThread(ThreadCache *tc);
  void unregisterThread(ThreadCache *tc);

  void installSigHandlers();
  void installSigAltStack();
  void removeSigAltStack();

  void quiesceSelf();

  static __thread stack_t _altStack;
  static __thread ThreadCache *_threadCache;

  friend Runtime &runtime();
  friend StopTheWorld;

  PthreadCreateFn _pthreadCreate{nullptr};
  GlobalHeap _heap{};
  mutex _mutex{};
  StopTheWorld _stw{};
  ThreadCache _mainCache{};
  ThreadCache *_caches{nullptr};
};

// get a reference to the Runtime singleton
inline Runtime &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime) + sizeof(double) - 1) / sizeof(double)];
  static Runtime *runtimePtr = new (buf) Runtime{};
  return *runtimePtr;
}
}

#endif  // MESH__RUNTIME_H
