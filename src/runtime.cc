// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#include <dirent.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "runtime.h"

__thread stack_t mesh::Runtime::_altStack;

namespace mesh {

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

void StopTheWorld::quiesceOthers() {
  static const size_t BUF_LEN = 2048;
  debug("quiesce others");

  auto taskDir = open("/proc/self/task", O_CLOEXEC | O_DIRECTORY | O_RDONLY);
  d_assert(taskDir > 0);

  char buf[BUF_LEN];

  internal::vector<pid_t> threadIds;
  pid_t self = syscall(SYS_gettid);

  off_t off = 0;

  memset(buf, 0, BUF_LEN);
  int len = getdirentries(taskDir, buf, BUF_LEN, &off);
  while (len > 0) {
    long dentOff = 0;
    while (dentOff < len) {
      auto dirent = reinterpret_cast<struct dirent *>(&buf[dentOff]);
      dentOff += dirent->d_reclen;

      if (strlen(dirent->d_name) == 0 || dirent->d_name[0] < '0' || dirent->d_name[0] > '9')
        continue;

      pid_t tid = atoi(dirent->d_name);
      if (tid != self)
        threadIds.push_back(tid);
    }

    memset(buf, 0, BUF_LEN);
    len = getdirentries(taskDir, buf, BUF_LEN, &off);
  }
  d_assert(len == 0);

  close(taskDir);

  if (threadIds.size() == 0) {
    debug("\tonly thread running, noone to quiet");
    return;
  }

  for (auto tid : threadIds) {
    debug("\tquiet %d", tid);
  }
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
