// -*- mode: c++ -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__RUNTIME_HH
#define MESH__RUNTIME_HH

#include <pthread.h>
#include <signal.h>  // for stack_t

#include "file-backed-mmapheap.hh"
#include "internal.hh"
#include "meshingheap.hh"

#include "heaplayers.h"

namespace mesh {

// function passed to pthread_create
typedef void *(*PthreadFn)(void *);

// signature of pthread_create itself
typedef int (*PthreadCreateFn)(pthread_t *thread, const pthread_attr_t *attr, PthreadFn start_routine, void *arg);

// The top heap provides memory to back spans managed by MiniHeaps.
class TopHeap : public ExactlyOneHeap<mesh::FileBackedMmapHeap> {
private:
  typedef ExactlyOneHeap<mesh::FileBackedMmapHeap> SuperHeap;

public:
  void mesh(void *keep, void *remove) {
    getSuperHeap().internalMesh(keep, remove);
  }
};

// The top big heap is called to handle malloc requests for large
// objects.  We define a separate class to handle these to segregate
// bookkeeping for large malloc requests from the ones used to back
// spans (which are allocated from TopHeap)
class TopBigHeap : public ExactlyOneHeap<mesh::MmapHeap> {};

// fewer buckets than regular KingsleyHeap (to ensure multiple objects
// fit in the 128Kb spans used by MiniHeaps).
class BottomHeap : public mesh::MeshingHeap<11, mesh::size2Class, mesh::class2Size, 20, TopHeap, TopBigHeap> {};

// TODO: remove the LockedHeap here and use a per-thread BottomHeap
class MeshHeap : public ANSIWrapper<LockedHeap<PosixLockType, BottomHeap>> {
private:
  DISALLOW_COPY_AND_ASSIGN(MeshHeap);
  typedef ANSIWrapper<LockedHeap<PosixLockType, BottomHeap>> SuperHeap;

public:
  explicit MeshHeap() : SuperHeap() {
  }
};

// forward declaration of runtime so we can declare it a friend to StopTheWorld
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
  void quiesce();
  void resume();
};

class Runtime {
private:
  DISALLOW_COPY_AND_ASSIGN(Runtime);

  // ensure we don't mistakenly create additional runtime instances
  explicit Runtime();

public:
  friend Runtime &runtime();
  friend StopTheWorld;

  void lock();
  void unlock();

  inline StopTheWorld &stopTheWorld() {
    return _stw;
  }

  inline MeshHeap &heap() {
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

  void installSigHandlers();
  void installSigAltStack();
  void removeSigAltStack();

  void quiesce();

  static __thread stack_t _altStack;

  PthreadCreateFn _pthreadCreate{nullptr};
  MeshHeap _heap{};
  mutex _mutex{};
  StopTheWorld _stw{};
};

// get a reference to the Runtime singleton
inline Runtime &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime) + sizeof(double) - 1) / sizeof(double)];
  static Runtime *runtimePtr = new (buf) Runtime{};
  return *runtimePtr;
}
}
#endif  // MESH__RUNTIME_HH
