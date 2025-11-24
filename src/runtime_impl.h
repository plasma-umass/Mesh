// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_RUNTIME_IMPL_H
#define MESH_RUNTIME_IMPL_H

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>

#ifdef __linux__
#include <sys/signalfd.h>
#endif

#include "runtime.h"
#include "thread_local_heap.h"

namespace mesh {

template <size_t PageSize>
void Runtime<PageSize>::initMaxMapCount() {
#ifndef __linux__
  return;
#endif

  auto fd = open("/proc/sys/vm/max_map_count", O_RDONLY | O_CLOEXEC);
  if (unlikely(fd < 0)) {
    mesh::debug("initMaxMapCount: no proc file");
    return;
  }

  static constexpr size_t BUF_LEN = 128;
  char buf[BUF_LEN];
  memset(buf, 0, BUF_LEN);

  auto _ __attribute__((unused)) = read(fd, buf, BUF_LEN - 1);
  close(fd);

  errno = 0;
  int64_t mapCount = strtoll(buf, nullptr, 10);
  if (errno != 0) {
    mesh::debug("strtoll: %s (%s)", strerror(errno), buf);
    return;
  }

  if (mapCount <= 0) {
    mesh::debug("expected positive mapCount, not %ll", mapCount);
    return;
  }

  const auto meshCount = static_cast<size_t>(kMeshesPerMap * mapCount);

  _heap.setMaxMeshCount(meshCount);
}

template <size_t PageSize>
int Runtime<PageSize>::createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg) {
  lock_guard<Runtime> lock(*this);

  if (unlikely(mesh::real::pthread_create == nullptr)) {
    mesh::real::init();
  }

  void *threadArgsBuf = mesh::internal::Heap().malloc(sizeof(StartThreadArgs));
  d_assert(threadArgsBuf != nullptr);
  StartThreadArgs *threadArgs = new (threadArgsBuf) StartThreadArgs(this, startRoutine, arg);

  return mesh::real::pthread_create(thread, attr, reinterpret_cast<PthreadFn>(startThread), threadArgs);
}

template <size_t PageSize>
void *Runtime<PageSize>::startThread(StartThreadArgs *threadArgs) {
  d_assert(threadArgs != nullptr);

  Runtime *runtime = threadArgs->runtime;
  PthreadFn startRoutine = threadArgs->startRoutine;
  void *arg = threadArgs->arg;

  mesh::internal::Heap().free(threadArgs);
  threadArgs = nullptr;

  runtime->installSegfaultHandler();

  return startRoutine(arg);
}

template <size_t PageSize>
void Runtime<PageSize>::exitThread(void *retval) {
  if (unlikely(mesh::real::pthread_exit == nullptr)) {
    mesh::real::init();
  }

  auto heap = ThreadLocalHeap<PageSize>::GetHeapIfPresent();
  if (heap != nullptr) {
    heap->releaseAll();
  }

  mesh::real::pthread_exit(retval);

  // pthread_exit doesn't return
  __builtin_unreachable();
}

template <size_t PageSize>
void Runtime<PageSize>::createSignalFd() {
  mesh::real::init();

#ifdef __linux__
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGDUMP);

  /* Block signals so that they aren't handled
     according to their default dispositions */

  auto result = mesh::real::sigprocmask(SIG_BLOCK, &mask, NULL);
  hard_assert(result == 0);

  _signalFd = signalfd(-1, &mask, 0);
  hard_assert(_signalFd >= 0);
#endif
}

template <size_t PageSize>
void Runtime<PageSize>::startBgThread() {
  constexpr int MaxRetries = 20;

  pthread_t bgPthread;
  int retryCount = 0;
  int ret = 0;

  while ((ret = pthread_create(&bgPthread, nullptr, Runtime::bgThread, nullptr))) {
    retryCount++;
    sched_yield();

    if (retryCount % 10)
      debug("background thread creation failed, retrying.\n");

    if (retryCount >= MaxRetries) {
      debug("max retries exceded: couldn't create bg thread, exiting.\n");
      abort();
    }
  }
}

template <size_t PageSize>
void *Runtime<PageSize>::bgThread(void *arg) {
  auto &rt = mesh::runtime<PageSize>();

  // debug("libmesh: background thread started\n");

#ifdef __linux__
  while (true) {
    struct signalfd_siginfo siginfo;

    ssize_t s = read(rt._signalFd, &siginfo, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
      if (s >= 0) {
        debug("bad read size: %lld\n", s);
        abort();
      } else {
        // read returns -1 if the program gets a process-killing signal
        return nullptr;
      }
    }

    if (static_cast<int>(siginfo.ssi_signo) == SIGDUMP) {
      // debug("libmesh: background thread received SIGDUMP, starting dump\n");
      debug(">>>>>>>>>>\n");
      rt.heap().dumpStrings();
      // debug("<<<<<<<<<<\n");

      // debug("<<<<<<<<<<\n");
    } else {
      auto _ __attribute__((unused)) =
          write(STDERR_FILENO, "Read unexpected signal\n", strlen("Read unexpected signal\n"));
    }
  }
#endif
  return nullptr;
}

template <size_t PageSize>
void Runtime<PageSize>::lock() {
  _mutex.lock();
}

template <size_t PageSize>
void Runtime<PageSize>::unlock() {
  _mutex.unlock();
}

#ifdef __linux__
template <size_t PageSize>
int Runtime<PageSize>::epollWait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout) {
  if (unlikely(mesh::real::epoll_wait == nullptr))
    mesh::real::init();

  _heap.maybeMesh();

  return mesh::real::epoll_wait(__epfd, __events, __maxevents, __timeout);
}

template <size_t PageSize>
int Runtime<PageSize>::epollPwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout,
                                  const __sigset_t *__ss) {
  if (unlikely(mesh::real::epoll_pwait == nullptr))
    mesh::real::init();

  _heap.maybeMesh();

  return mesh::real::epoll_pwait(__epfd, __events, __maxevents, __timeout, __ss);
}

template <size_t PageSize>
ssize_t Runtime<PageSize>::recv(int sockfd, void *buf, size_t len, int flags) {
  if (unlikely(mesh::real::recv == nullptr)) {
    mesh::real::init();
  }
  ssize_t ret = mesh::real::recv(sockfd, buf, len, flags);
  while (ret < 0 && errno == EFAULT && heap().okToProceed(buf)) {
    ret = mesh::real::recv(sockfd, buf, len, flags);
  }
  return ret;
}

template <size_t PageSize>
ssize_t Runtime<PageSize>::recvmsg(int sockfd, struct msghdr *msg, int flags) {
  if (unlikely(mesh::real::recvmsg == nullptr))
    mesh::real::init();

  ssize_t ret = mesh::real::recvmsg(sockfd, msg, flags);
  while (ret < 0 && errno == EFAULT && msg) {
    for (size_t i = 0; i < msg->msg_iovlen; ++i) {
      auto ptr = msg->msg_iov[i].iov_base;
      if (ptr) {
        heap().okToProceed(ptr);
      }
    }
    ret = mesh::real::recvmsg(sockfd, msg, flags);
  }
  return ret;
}

#endif

static struct sigaction sigbusAction;
static struct sigaction sigsegvAction;
static mutex sigactionLock;

template <size_t PageSize>
int Runtime<PageSize>::sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
  if (unlikely(mesh::real::sigaction == nullptr)) {
    mesh::real::init();
  }

  if (signum != SIGSEGV && signum != SIGBUS) {
    return mesh::real::sigaction(signum, act, oldact);
  }

  // if a user is trying to install a segfault handler, record that
  // here to proxy to later.
  lock_guard<mutex> lock(sigactionLock);

  auto nextAct = &sigsegvAction;
  if (signum == SIGBUS) {
    act = &sigbusAction;
  }

  if (oldact)
    memcpy(oldact, nextAct, sizeof(*nextAct));

  if (act == nullptr) {
    memset(nextAct, 0, sizeof(*nextAct));
  } else {
    // debug("TODO: user installed a segfault handler");
    memcpy(nextAct, act, sizeof(*nextAct));
  }

  return 0;
}

template <size_t PageSize>
int Runtime<PageSize>::sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  if (unlikely(mesh::real::sigprocmask == nullptr))
    mesh::real::init();

  lock_guard<mutex> lock(sigactionLock);

  // debug("TODO: ensure we never mask SIGSEGV\n");

  return mesh::real::sigprocmask(how, set, oldset);
}

template <size_t PageSize>
void Runtime<PageSize>::segfaultHandler(int sig, siginfo_t *siginfo, void *context) {
  if (runtime<PageSize>().pid() != getpid()) {
    // we are just after fork, and glibc sucks.
    runtime<PageSize>().heap().doAfterForkChild();
  }

  // okToProceed is a barrier that ensures any in-progress meshing has
  // completed, and the reason for the fault was 'just' a meshing
  bool isMeshingFault = false;

#ifdef __APPLE__
  // On macOS, mprotect violations can trigger SIGBUS with BUS_ADRERR or BUS_ADRALN
  if (sig == SIGBUS && (siginfo->si_code == BUS_ADRERR || siginfo->si_code == BUS_ADRALN)) {
    isMeshingFault = true;
  }
#endif

  // On Linux and other systems, mprotect violations trigger SIGSEGV with SEGV_ACCERR
  if (siginfo->si_code == SEGV_ACCERR) {
    isMeshingFault = true;
  }

  if (isMeshingFault && runtime<PageSize>().heap().okToProceed(siginfo->si_addr)) {
    // debug("TODO: trapped access violation from meshing, log stat\n");
    return;
  }

  struct sigaction *action = nullptr;
  if (sig == SIGBUS) {
    action = &sigbusAction;
  } else {
    action = &sigsegvAction;
  }

  if (action != nullptr) {
    if (action->sa_sigaction != nullptr) {
      action->sa_sigaction(sig, siginfo, context);
      return;
    } else if (action->sa_handler == SIG_IGN) {
      // ignore
      return;
    } else if (action->sa_handler != nullptr && action->sa_handler != SIG_DFL) {
      action->sa_handler(sig);
      return;
    }
  }

  if (siginfo->si_code == SEGV_MAPERR && siginfo->si_addr == nullptr) {
    debug("libmesh: caught null pointer dereference (signal: %d)", sig);
    raise(SIGABRT);
    _Exit(1);
  } else {
    debug("segfault (%u/%p): in arena? %d\n", siginfo->si_code, siginfo->si_addr,
          runtime<PageSize>().heap().contains(siginfo->si_addr));
    raise(SIGABRT);
    _Exit(1);
  }
}

template <size_t PageSize>
void Runtime<PageSize>::installSegfaultHandler() {
  struct sigaction action;
  struct sigaction oldAction;

  memset(&action, 0, sizeof(action));
  memset(&oldAction, 0, sizeof(oldAction));

  action.sa_sigaction = segfaultHandler;
  action.sa_flags = SA_SIGINFO | SA_NODEFER;

  auto err = mesh::real::sigaction(SIGBUS, &action, &oldAction);
  hard_assert(err == 0);

  lock_guard<mutex> lock(sigactionLock);

  if (oldAction.sa_sigaction != nullptr && oldAction.sa_sigaction != segfaultHandler) {
    // debug("TODO: oldAction not null: %p\n", (void *)oldAction.sa_sigaction);
    memcpy(&sigbusAction, &oldAction, sizeof(sigbusAction));
  }

  err = mesh::real::sigaction(SIGSEGV, &action, &oldAction);
  hard_assert(err == 0);

  if (oldAction.sa_sigaction != nullptr && oldAction.sa_sigaction != segfaultHandler) {
    // debug("TODO: oldAction not null: %p\n", (void *)oldAction.sa_sigaction);
    memcpy(&sigsegvAction, &oldAction, sizeof(sigsegvAction));
  }
}

}  // namespace mesh

#endif  // MESH_RUNTIME_IMPL_H
