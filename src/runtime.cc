// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <dirent.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "runtime.h"

namespace mesh {

__thread stack_t Runtime::_altStack;
__thread ThreadCache *Runtime::_threadCache;

STLAllocator<char, internal::Heap> internal::allocator{};

int internal::copyFile(int dstFd, int srcFd, size_t sz) {
#if defined(__APPLE__) || defined(__FreeBSD__)
  // fcopyfile works on FreeBSD and OS X 10.5+
  int result = fcopyfile(srcFd, dstFd, 0, COPYFILE_ALL);
#else
  // sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
  off_t bytesCopied = 0;
  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(srcFd, &fileinfo);
  d_assert_msg(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == sz, "copyfile: expected %zu == %zu",
               fileinfo.st_size, sz);
  int result = sendfile(dstFd, srcFd, &bytesCopied, sz);
  // on success, ensure the entire results were copied
  if (result == 0)
    d_assert(bytesCopied == fileinfo.st_size);
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
  d_assert(Runtime::_threadCache->_waiting);

  // FIXME: remove while loop
  while (true) {
    int64_t nextEpoch = -1;
    {
      lock_guard<mutex> lock(_sharedMu);
      nextEpoch = _resumeEpoch.load() + 1;
      Runtime::_threadCache->_waiting = false;
      if (_waiters.fetch_sub(1) == 1)
        _waitersCv.notify_one();
    }
    d_assert(nextEpoch > -1);

    {
      std::unique_lock<mutex> lock(_sharedMu);
      while (_resumeEpoch.load() < nextEpoch)
        _resumeCv.wait(lock);
      // if (!Runtime::_threadCache->_waiting)
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

  const auto self = Runtime::_threadCache;
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
  _caches = &_mainCache;
  _threadCache = &_mainCache;

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

ThreadCache::ThreadCache() : _tid(pthread_self()), _prev{this}, _next{this} {
}

void *Runtime::startThread(StartThreadArgs *threadArgs) {
  d_assert(threadArgs != nullptr);

  Runtime *runtime = threadArgs->runtime;
  PthreadFn startRoutine = threadArgs->startRoutine;
  void *arg = threadArgs->arg;

  mesh::internal::Heap().free(threadArgs);
  threadArgs = nullptr;

  void *buf = internal::Heap().malloc(sizeof(ThreadCache));
  ThreadCache *tc = new (buf) ThreadCache;

  runtime->installSigAltStack();
  runtime->registerThread(tc);

  void *result = startRoutine(arg);

  runtime->unregisterThread(tc);
  runtime->removeSigAltStack();

  return result;
}

void Runtime::sigQuiesceHandler(int sig, siginfo_t *info, void *uctx) {
  runtime()._stw.quiesceSelf();
}

void Runtime::registerThread(ThreadCache *tc) {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGQUIESCE);

  // make sure this thread is able to handle our SIGQUIESCE signal
  int result = sigprocmask(SIG_UNBLOCK, &sigset, nullptr);
  d_assert(result == 0);

  // runtime()._heap.lock();

  d_assert(_caches != nullptr);
  d_assert(tc->_next == tc);
  d_assert(tc->_prev == tc);

  auto next = _caches->_next;
  _caches->_next = tc;
  tc->_next = next;
  tc->_prev = _caches;
  next->_prev = tc;

  _threadCache = tc;

  // runtime()._heap.unlock();
}

void Runtime::unregisterThread(ThreadCache *tc) {
  _threadCache->_shutdownEpoch.exchange(_stw._resumeEpoch);

  // TODO: do this asynchronously in a subsequent epoch.  We can't
  // free this here, because the thread cache structure may be used
  // when destroying thread-local storage (which happens after this
  // function is called)

  // auto prev = tc->_prev;
  // auto next = tc->_next;
  // prev->_next = next;
  // next->_prev = prev;
  // tc->_next = tc;
  // tc->_prev = tc;
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
