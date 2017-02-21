// Copyright 2017 University of Massachusetts, Amherst

#include <dirent.h>

#include "runtime.h"

__thread stack_t mesh::Runtime::_altStack;

namespace mesh {

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

void StopTheWorld::lock() {
  runtime()._heap.lock();

  quiesceOthers();
}

void StopTheWorld::unlock() {
  resume();

  runtime()._heap.unlock();
}

void StopTheWorld::quiesceOthers() {
  debug("quiesce others");

  auto fd = open("/proc/self/task", O_CLOEXEC | O_DIRECTORY | O_RDONLY);
  d_assert(fd > 0);
}

void StopTheWorld::resume() {
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

void *Runtime::startThread(StartThreadArgs *threadArgs) {
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

void Runtime::sigQuiesceHandler(int sig, siginfo_t *info, void *uctx) {
  runtime().quiesceSelf();
}

void Runtime::quiesceSelf() {
  debug("quiesce myself");

  // currentIsQuiesced

  // waitToContinue
}

void Runtime::installSigHandlers() {
  struct sigaction sigQuiesce;
  memset(&sigQuiesce, 0, sizeof(sigQuiesce));
  // no need to explicitly set SIGQUIESCE in the mask - it is
  // automatically blocked while the handler is running.
  sigemptyset(&sigQuiesce.sa_mask);
  sigQuiesce.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
  sigQuiesce.sa_sigaction = sigQuiesceHandler;
  if (sigaction(internal::SIGQUIESCE, &sigQuiesce, NULL) == -1) {
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
