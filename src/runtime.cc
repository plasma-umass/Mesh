// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <dirent.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "runtime.h"

namespace mesh {

__thread stack_t Runtime::_altStack;
__thread STWThreadState *Runtime::_stwState;
__thread LocalHeap *Runtime::_localHeap;

STLAllocator<char, internal::Heap> internal::allocator{};

int internal::copyFile(int dstFd, int srcFd, off_t off, size_t sz) {
#if defined(__APPLE__) || defined(__FreeBSD__)
#error FIXME: use off
  // fcopyfile works on FreeBSD and OS X 10.5+
  int result = fcopyfile(srcFd, dstFd, 0, COPYFILE_ALL);
#else
  d_assert(off >= 0);

  off_t newOff = lseek(dstFd, off, SEEK_SET);
  d_assert(newOff == off);

  errno = 0;
  // sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
  int result = sendfile(dstFd, srcFd, &off, sz);
#endif

  return result;
}

void internal::StopTheWorld() noexcept {
  runtime().stopTheWorld().lock();
}

void internal::StartTheWorld() noexcept {
  runtime().stopTheWorld().unlock();
}

void StopTheWorld::lock() {
  // checks for meshing happen inside of free, so we already hold the
  // heap lock
  // runtime()._heap.lock();

  quiesceOthers();
}

void StopTheWorld::unlock() {
  resume();

  // runtime()._heap.unlock();
}

void StopTheWorld::quiesceSelf() {
  // debug("%x/%d: shutting myself up\n", pthread_self(), syscall(SYS_gettid));
  d_assert(Runtime::_stwState->_waiting);

  // FIXME: remove while loop
  while (true) {
    int64_t nextEpoch = -1;
    {
      lock_guard<mutex> lock(_sharedMu);
      nextEpoch = _resumeEpoch.load() + 1;
      Runtime::_stwState->_waiting = false;
      if (_waiters.fetch_sub(1) == 1)
        _waitersCv.notify_one();
    }
    d_assert(nextEpoch > -1);

    {
      std::unique_lock<mutex> lock(_sharedMu);
      while (_resumeEpoch.load() < nextEpoch)
        _resumeCv.wait(lock);
      // if (!Runtime::_stwState->_waiting)
      //   debug("%x/%d:\t\t\t\t expecting reschedule\n", pthread_self(), syscall(SYS_gettid));
      break;
    }
  }

  // if (_resumeEpoch.load() > nextEpoch)
  //   debug("%x: \tmaybe missed an epoch?", pthread_self());
  // debug("%x: was quiet, resuming\n", pthread_self());
}

void StopTheWorld::quiesceOthers() {
  d_assert(_waiters == 0);

  const auto self = Runtime::_stwState;
  size_t count = 0;
  for (auto tc = self->_next; tc != self; tc = tc->_next) {
    if (tc->_shutdownEpoch > -1)
      continue;
    ++count;
  }

  // if there are no other threads, nothing to quiesce
  if (count == 0)
    return;

  // debug("%x/%d: STOP THE WORLD (count: %d)\n", pthread_self(), syscall(SYS_gettid), count);

  {
    lock_guard<mutex> lock(_sharedMu);
    ++_resumeEpoch;
  }

  _waiters = count;

  size_t killCount = 0;
  const pthread_t selfId = pthread_self();

  for (auto tc = self->_next; tc != self; tc = tc->_next) {
    if (tc->_shutdownEpoch > -1)
      continue;
    tc->_waiting = true;
    d_assert(tc->_tid != selfId);
    // debug("\tkill %x\n", tc->_tid);
    pthread_kill(tc->_tid, SIGQUIESCE);
    ++killCount;
  }
  d_assert(count == killCount);

  {
    std::unique_lock<mutex> lock(_sharedMu);
    _waitersCv.wait(lock, [this] { return _waiters == 0; });
  }

  // debug("%x: WORLD STOPPED", pthread_self());
}

void StopTheWorld::resume() {
  std::unique_lock<mutex> lock(_sharedMu);

  ++_resumeEpoch;
  lock.unlock();
  _resumeCv.notify_all();
}

Runtime::Runtime() {
  installSigAltStack();
  installSigHandlers();
}

void Runtime::lock() {
  _mutex.lock();
}

void Runtime::unlock() {
  _mutex.unlock();
}

int Runtime::createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg) {
  lock_guard<Runtime> lock(*this);

  if (_pthreadCreate == nullptr) {
    initThreads();
  }

  void *threadArgsBuf = mesh::internal::Heap().malloc(sizeof(StartThreadArgs));
  d_assert(threadArgsBuf != nullptr);
  StartThreadArgs *threadArgs = new (threadArgsBuf) StartThreadArgs(this, startRoutine, arg);

  return _pthreadCreate(thread, attr, reinterpret_cast<PthreadFn>(startThread), threadArgs);
}

void Runtime::initThreads() {
  // FIXME: this assumes glibc
  void *pthreadHandle = dlopen("libpthread.so.0", RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
  d_assert(pthreadHandle != nullptr);

  auto createFn = dlsym(pthreadHandle, "pthread_create");
  d_assert(createFn != nullptr);

  _pthreadCreate = reinterpret_cast<PthreadCreateFn>(createFn);
}

STWThreadState::STWThreadState() : _tid(pthread_self()), _prev{this}, _next{this} {
}

void Runtime::allocLocalHeap() {
  d_assert(_localHeap == nullptr);

  void *buf = mesh::internal::Heap().malloc(sizeof(LocalHeap));
  if (buf == nullptr) {
    mesh::debug("mesh: unable to allocate LocalHeap, aborting.\n");
    abort();
  }
  _localHeap = new (buf) LocalHeap(&mesh::runtime().heap());
}

void *Runtime::startThread(StartThreadArgs *threadArgs) {
  d_assert(threadArgs != nullptr);

  Runtime *runtime = threadArgs->runtime;
  PthreadFn startRoutine = threadArgs->startRoutine;
  void *arg = threadArgs->arg;

  mesh::internal::Heap().free(threadArgs);
  threadArgs = nullptr;

  auto stwState = runtime->localHeap()->stwState();

  runtime->installSigAltStack();
  runtime->registerThread(stwState);

  void *result = startRoutine(arg);

  runtime->unregisterThread(stwState);
  runtime->removeSigAltStack();

  return result;
}

void Runtime::sigQuiesceHandler(int sig, siginfo_t *info, void *uctx) {
  runtime()._stw.quiesceSelf();
}

void Runtime::registerThread(STWThreadState *stwState) {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGQUIESCE);

  // make sure this thread is able to handle our SIGQUIESCE signal
  int result = sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
  d_assert(result == 0);

  // runtime()._heap.lock();

  stwState->insert(_threads);

  _stwState = stwState;

  // runtime()._heap.unlock();
}

void Runtime::unregisterThread(STWThreadState *stwState) {
  stwState->unregister(_stw._resumeEpoch);
}

void Runtime::installSigHandlers() {
  struct sigaction sigQuiesce;
  memset(&sigQuiesce, 0, sizeof(sigQuiesce));
  // no need to explicitly set SIGQUIESCE in the mask - it is
  // automatically blocked while the handler is running.
  sigemptyset(&sigQuiesce.sa_mask);
  sigQuiesce.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigQuiesce.sa_sigaction = sigQuiesceHandler;
  if (sigaction(SIGQUIESCE, &sigQuiesce, NULL) == -1) {
    debug("sigaction(SIGQUIESCE): %d", errno);
    abort();
  }
}

void Runtime::installSigAltStack() {
  static_assert(internal::ALTSTACK_SIZE >= MINSIGSTKSZ, "altstack not big enough");
  d_assert(_altStack.ss_sp == nullptr);

  memset(&_altStack, 0, sizeof(_altStack));
  // FIXME: should we consider allocating altstack from mmap directly?
  _altStack.ss_sp = internal::Heap().malloc(internal::ALTSTACK_SIZE);
  _altStack.ss_size = internal::ALTSTACK_SIZE;

  sigaltstack(&_altStack, nullptr);
}

void Runtime::removeSigAltStack() {
  d_assert(_altStack.ss_sp != nullptr);

  stack_t disableStack;

  memset(&disableStack, 0, sizeof(disableStack));
  disableStack.ss_flags = SS_DISABLE;

  sigaltstack(&disableStack, nullptr);

  internal::Heap().free(_altStack.ss_sp);
  _altStack.ss_sp = nullptr;
  _altStack.ss_size = 0;
}
}
