// -*- mode: c++ -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__RUNTIME_HH
#define MESH__RUNTIME_HH

#include <pthread.h>
#include <signal.h>

#include "file-backed-mmapheap.hh"
#include "internal.hh"
#include "meshingheap.hh"

#include "heaplayers.h"

namespace mesh {

// signature of function passed to pthread_create
typedef void *(*PthreadFn)(void *);

// signature of pthread_create
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

class Runtime {
private:
  DISALLOW_COPY_AND_ASSIGN(Runtime);
  explicit Runtime() {
  }
public:
  friend Runtime &runtime();

  void lock() {
  }

  void unlock() {
  }

  inline MeshHeap &heap() {
    return _heap;
  }

  int createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg) {
    // FIXME: locking
    if (_pthreadCreate == nullptr) {
      initThreads();
    }
    void *threadArgsBuf = mesh::internal::Heap().malloc(sizeof(StartThreadArgs));
    d_assert(threadArgsBuf != nullptr);
    StartThreadArgs *threadArgs = new (threadArgsBuf) StartThreadArgs(this, startRoutine, arg);

    return _pthreadCreate(thread, attr, reinterpret_cast<PthreadFn>(startThread), threadArgs);
  }

private:
  // initialize our pointer to libc's pthread_create.  This happens
  // lazily, as the dynamic linker calls into malloc for dynamic
  // memory allocation, so if we try to do this in MeshHeaps's
  // constructor we deadlock before main even runs.
  void initThreads() {
    // FIXME: this assumes glibc
    void *pthreadHandle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    d_assert(pthreadHandle != nullptr);

    auto createFn = dlsym(pthreadHandle, "pthread_create");
    d_assert(createFn != nullptr);

    _pthreadCreate = reinterpret_cast<PthreadCreateFn>(createFn);
  }

  struct StartThreadArgs {
    explicit StartThreadArgs(Runtime *runtime_, PthreadFn startRoutine_, void *arg_)
        : runtime(runtime_), startRoutine(startRoutine_), arg(arg_) {
    }

    Runtime *runtime;
    PthreadFn startRoutine;
    void *arg;
  };

  static void *startThread(StartThreadArgs *threadArgs) {
    d_assert(threadArgs != nullptr);

    Runtime *runtime = threadArgs->runtime;
    PthreadFn startRoutine = threadArgs->startRoutine;
    void *arg = threadArgs->arg;

    mesh::internal::Heap().free(threadArgs);
    threadArgs = nullptr;

    runtime->installSigAltStack();

    void *result = startRoutine(arg);

    runtime->removeSigAltStack();

    return result;
  }

  void installSigAltStack() {
    // TODO: install sigaltstack
    debug("TODO: install sigaltstack");
  }

  void removeSigAltStack() {
    // TODO: remove sigaltstack
  }

  static __thread stack_t _altStack;

  PthreadCreateFn _pthreadCreate{nullptr};
  MeshHeap _heap{};
};

inline Runtime &runtime() {
  // force alignment by using a buffer of doubles.
  static double buf[(sizeof(Runtime) + sizeof(double) - 1) / sizeof(double)];
  static Runtime *runtimePtr = new (buf) Runtime{};
  return *runtimePtr;
}
}
#endif  // MESH__RUNTIME_HH
