// Copyright 2017 University of Massachusetts, Amherst

#include "runtime.hh"

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
}

void StopTheWorld::unlock() {
  runtime()._heap.unlock();
}

void Runtime::lock() {
  _heap.lock();
}

void Runtime::unlock() {
  _heap.unlock();
}

int Runtime::createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg) {
  // FIXME: locking
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

void Runtime::installSigAltStack() {
  // TODO: install sigaltstack
  debug("TODO: install sigaltstack");
}

void Runtime::removeSigAltStack() {
  // TODO: remove sigaltstack
}
}
