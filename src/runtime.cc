// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/prctl.h>

#ifdef __linux__
#include <sys/signalfd.h>
#endif

#include "runtime.h"
#include "thread_local_heap.h"

namespace mesh {

ATTRIBUTE_ALIGNED(CACHELINE_SIZE)
const unsigned char SizeMap::class_array_[kClassArraySize] = {
#include "size_classes.def"
};

ATTRIBUTE_ALIGNED(CACHELINE_SIZE)
const int32_t SizeMap::class_to_size_[kClassSizesMax] = {
    16,  16,  32,  48,  64,  80,  96,  112,  128,  160,  192,  224,   256,
    320, 384, 448, 512, 640, 768, 896, 1024, 2048, 4096, 8192, 16384,
};

// const internal::BinToken::Size internal::BinToken::Max = numeric_limits<uint32_t>::max();
// const internal::BinToken::Size internal::BinToken::MinFlags = numeric_limits<uint32_t>::max() - 4;

// const internal::BinToken::Size internal::BinToken::FlagFull = numeric_limits<uint32_t>::max() - 1;
// const internal::BinToken::Size internal::BinToken::FlagEmpty = numeric_limits<uint32_t>::max() - 2;
// const internal::BinToken::Size internal::BinToken::FlagNoOff = numeric_limits<uint32_t>::max();

STLAllocator<char, internal::Heap> internal::allocator{};

size_t internal::measurePssKiB() {
  auto fd = open("/proc/self/smaps_rollup", O_RDONLY | O_CLOEXEC);
  if (unlikely(fd < 0)) {
    mesh::debug("measurePssKiB: no smaps_rollup");
    return 0;
  }

  static constexpr size_t BUF_LEN = 1024;
  char buf[BUF_LEN];
  memset(buf, 0, BUF_LEN);

  auto _ __attribute__((unused)) = read(fd, buf, BUF_LEN - 1);
  close(fd);

  auto start = strstr(buf, "\nPss: ");
  if (unlikely(start == nullptr)) {
    mesh::debug("measurePssKiB: no Pss");
    return 0;
  }

  return atoi(&start[6]);
}

#if defined(__linux__) || defined(__linux)
inline static void change_thread_name(const char *postfix, int index = -1) {
  char process_name[16];
  prctl(PR_GET_NAME, (unsigned long)process_name, 0, 0, 0);
  char new_name[16];
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif
  if (index < 0)
    snprintf(new_name, 16, "%s[%s]", process_name, postfix);
  else
    snprintf(new_name, 16, "%s[%s%d]", process_name, postfix, index);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
  prctl(PR_SET_NAME, (unsigned long)new_name, 0, 0, 0);
}
#else
inline void change_thread_name(const char *, int = -1) {
}
#endif

int internal::copyFile(int dstFd, int srcFd, off_t off, size_t sz) {
  d_assert(off >= 0);

  off_t newOff = lseek(dstFd, off, SEEK_SET);
  d_assert(newOff == off);

#if defined(__APPLE__) || defined(__FreeBSD__)
#warning test that setting offset on dstFd works as intended
  // fcopyfile works on FreeBSD and OS X 10.5+
  int result = fcopyfile(srcFd, dstFd, 0, COPYFILE_ALL);
#else
  errno = 0;
  // sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
  int result = sendfile(dstFd, srcFd, &off, sz);
#endif

  return result;
}

Runtime::Runtime() {
  static_assert(sizeof(FreeCmdRingVector) < 4096, "tlh should have a reasonable size");

  void *buf = mesh::internal::Heap().malloc(sizeof(FreeCmdRingVector));
  hard_assert(buf != nullptr);
  hard_assert(reinterpret_cast<uintptr_t>(buf) % CACHELINE_SIZE == 0);

  _pagesFreeCmdBuffer = new (buf) FreeCmdRingVector(32);

  buf = mesh::internal::Heap().malloc(sizeof(FreeCmdRingVector));
  hard_assert(buf != nullptr);
  hard_assert(reinterpret_cast<uintptr_t>(buf) % CACHELINE_SIZE == 0);

  _pagesReturnCmdBuffer = new (buf) FreeCmdRingVector(32);

  updatePid();
}

void Runtime::initMaxMapCount() {
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

int Runtime::createThread(pthread_t *thread, const pthread_attr_t *attr, PthreadFn startRoutine, void *arg) {
  lock_guard<Runtime> lock(*this);

  if (unlikely(mesh::real::pthread_create == nullptr)) {
    mesh::real::init();
  }

  void *threadArgsBuf = mesh::internal::Heap().malloc(sizeof(StartThreadArgs));
  d_assert(threadArgsBuf != nullptr);
  StartThreadArgs *threadArgs = new (threadArgsBuf) StartThreadArgs(this, startRoutine, arg);

  return mesh::real::pthread_create(thread, attr, reinterpret_cast<PthreadFn>(startThread), threadArgs);
}

void *Runtime::startThread(StartThreadArgs *threadArgs) {
  d_assert(threadArgs != nullptr);

  Runtime *runtime = threadArgs->runtime;
  PthreadFn startRoutine = threadArgs->startRoutine;
  void *arg = threadArgs->arg;

  mesh::internal::Heap().free(threadArgs);
  threadArgs = nullptr;

  runtime->installSegfaultHandler();

  return startRoutine(arg);
}

void Runtime::exitThread(void *retval) {
  if (unlikely(mesh::real::pthread_exit == nullptr)) {
    mesh::real::init();
  }

  auto heap = ThreadLocalHeap::GetHeapIfPresent();
  if (heap != nullptr) {
    heap->releaseAll();
  }

  mesh::real::pthread_exit(retval);

  // pthread_exit doesn't return
  __builtin_unreachable();
}

void Runtime::createSignalFd() {
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

void Runtime::startBgThread() {
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

void *Runtime::bgThread(void *arg) {
  change_thread_name("bg");

  auto &rt = mesh::runtime();
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

void Runtime::startFreePhysThread() {
  constexpr int MaxRetries = 20;

  pthread_t bgPthread;
  int retryCount = 0;
  int ret = 0;

  while ((ret = pthread_create(&bgPthread, nullptr, Runtime::bgFreePhysThread, nullptr))) {
    retryCount++;
    sched_yield();

    if (retryCount % 10)
      debug("freePhys thread creation failed, retrying.\n");

    if (retryCount >= MaxRetries) {
      debug("max retries exceded: couldn't create freePhys thread, exiting.\n");
      abort();
    }
  }
}

size_t mergeSpans(internal::vector<Span> &spans) {
  size_t mergeCount = 0;

  if (spans.size() > 1) {
    // sort and merge spans
    // std::sort(spans.begin(), spans.end());

    internal::vector<Span> tmpSpans;
    tmpSpans.reserve(spans.size());

    auto leftIt = spans.begin();
    auto rightIt = spans.begin() + 1;

    while (rightIt != spans.end()) {
      d_assert(leftIt->offset + leftIt->length <= rightIt->offset);

      if (leftIt->offset + leftIt->length == rightIt->offset) {
        leftIt->length += rightIt->length;
        ++mergeCount;
        ++rightIt;
        continue;
      } else {
        tmpSpans.emplace_back(leftIt->offset, leftIt->length);
        leftIt = rightIt;
        ++rightIt;
      }
    }
    tmpSpans.emplace_back(leftIt->offset, leftIt->length);
    spans.swap(tmpSpans);
  }

  return mergeCount;
}

bool Runtime::jobFreeCmd() {
  auto &rt = mesh::runtime();
  internal::FreeCmd *fCommand = rt._pagesFreeCmdBuffer->pop();

  if (fCommand) {
#ifndef NDEBUG
    size_t pageCount = 0;
#endif

    switch (fCommand->cmd) {
    case internal::FreeCmd::FREE_PAGE: {
      for (auto &span : fCommand->spans) {
        rt.heap().freePhys(span);
#ifndef NDEBUG
        pageCount += span.length;
#endif
      }
      // debug("FREE_PAGE: %d spans,  pageCount = %d\n", fCommand->spans.size(), pageCount);
      delete fCommand;
      break;
    }

    case internal::FreeCmd::UNMAP_PAGE: {
      for (auto &span : fCommand->spans) {
        rt.heap().resetSpanMapping(span);
#ifndef NDEBUG
        pageCount += span.length;
#endif
      }
      rt.expandFlushSpans(fCommand->spans, false);
      // debug("UNMAP_PAGE: %d spans,  pageCount = %d\n", fCommand->spans.size(), pageCount);
      delete fCommand;
      break;
    }

    case internal::FreeCmd::FREE_DIRTY_PAGE: {
      std::sort(fCommand->spans.begin(), fCommand->spans.end());
      mergeSpans(fCommand->spans);
      for (auto &span : fCommand->spans) {
        rt.heap().freePhys(span);
#ifndef NDEBUG
        pageCount += span.length;
#endif
      }

      rt.expandFlushSpans(fCommand->spans, true);
      // debug("FREE_DIRTY_PAGE: %d spans,  pageCount = %d\n", fCommand->spans.size(), pageCount);
      delete fCommand;
      break;
    }
    case internal::FreeCmd::CLEAN_PAGE: {
#ifndef NDEBUG
      for (auto &span : fCommand->spans) {
        pageCount += span.length;
      }
#endif

      rt.expandFlushSpans(fCommand->spans, false);
      // debug("CLEAN_PAGE: %d spans,  pageCount = %d\n", fCommand->spans.size(), pageCount);
      delete fCommand;
      break;
    }

    case internal::FreeCmd::FLUSH: {
      auto &spans = rt.getFlushSpans();
      mergeSpans(spans);
      // debug("FLUSH mergeSpans:  merge: %d -> %d\n", mergeCount, spans.size());
      fCommand->spans.swap(spans);
      rt._pagesReturnCmdBuffer->push(fCommand);
      break;
    }

    default:
      hard_assert(false);
      break;
    }

    return false;
  } else {
    return true;
  }
}

void Runtime::autoFlush() {
  auto &rt = mesh::runtime();
  auto &spans = rt.getFlushSpans();
  mergeSpans(spans);
  // debug("AUTO FLUSH mergeSpans:  merge: page:%d, vsize:%d\n", mergeCount, spans.size());
  internal::FreeCmd *fCommand = new internal::FreeCmd(internal::FreeCmd::FLUSH);
  fCommand->spans.swap(spans);

  bool ok = rt._pagesReturnCmdBuffer->try_push(fCommand);
  if (!ok) {
    // debug("AUTO FLUSH failed");
    fCommand->spans.swap(spans);
    delete fCommand;
  }
}

void *Runtime::bgFreePhysThread(void *arg) {
  change_thread_name("mesh");

  auto &rt = mesh::runtime();
  rt._freeThreadRunning = true;
  // debug("libmesh: freePhys thread started\n");

  size_t idle_count = 0;
  size_t processCount = 0;
  while (true) {
    auto idle = rt.jobFreeCmd();

    if (idle) {
      ++idle_count;
      ++processCount;
    } else {
      idle_count = 0;
    }

    if (processCount > 16) {
      auto &spans = rt.getFlushSpans();
      if (spans.size() > 1) {
        rt.autoFlush();
        processCount = 0;
      }
    }

    if (idle_count > 100) {
      std::this_thread::sleep_for(1ms);
    }
  }

  return nullptr;
}

void Runtime::lock() {
  _mutex.lock();
}

void Runtime::unlock() {
  _mutex.unlock();
}

#ifdef __linux__
int Runtime::epollWait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout) {
  if (unlikely(mesh::real::epoll_wait == nullptr))
    mesh::real::init();

  _heap.maybeMesh();

  return mesh::real::epoll_wait(__epfd, __events, __maxevents, __timeout);
}

int Runtime::epollPwait(int __epfd, struct epoll_event *__events, int __maxevents, int __timeout,
                        const __sigset_t *__ss) {
  if (unlikely(mesh::real::epoll_pwait == nullptr))
    mesh::real::init();

  _heap.maybeMesh();

  return mesh::real::epoll_pwait(__epfd, __events, __maxevents, __timeout, __ss);
}
#endif

static struct sigaction sigbusAction;
static struct sigaction sigsegvAction;
static mutex sigactionLock;

int Runtime::sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
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

int Runtime::sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  if (unlikely(mesh::real::sigprocmask == nullptr))
    mesh::real::init();

  lock_guard<mutex> lock(sigactionLock);

  // debug("TODO: ensure we never mask SIGSEGV\n");

  return mesh::real::sigprocmask(how, set, oldset);
}

void Runtime::segfaultHandler(int sig, siginfo_t *siginfo, void *context) {
  if (runtime().pid() != getpid()) {
    // we are just after fork, and glibc sucks.
    runtime().heap().doAfterForkChild();
  }

  // okToProceed is a barrier that ensures any in-progress meshing has
  // completed, and the reason for the fault was 'just' a meshing
  if (siginfo->si_code == SEGV_ACCERR && runtime().heap().okToProceed(siginfo->si_addr)) {
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
          runtime().heap().contains(siginfo->si_addr));
    raise(SIGABRT);
    _Exit(1);
  }
}

void Runtime::installSegfaultHandler() {
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
