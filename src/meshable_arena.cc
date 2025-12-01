// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include "meshable_arena.h"
#include "runtime.h"

namespace mesh {

template <size_t PageSize>
void MeshableArena<PageSize>::prepareForFork() {
  if (!kMeshingEnabled) {
    return;
  }

  runtime<PageSize>().heap().lock();
  runtime<PageSize>().lock();
  internal::Heap().lock();

  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ);
  hard_assert(r == 0);

  int err = pipe(_forkPipe);
  if (err == -1) {
    abort();
  }
}

template <size_t PageSize>
void MeshableArena<PageSize>::afterForkParent() {
  if (!kMeshingEnabled) {
    return;
  }

  internal::Heap().unlock();

  close(_forkPipe[1]);

  char buf[8];
  memset(buf, 0, 8);

  while (read(_forkPipe[0], buf, 4) == EAGAIN) {
  }
  close(_forkPipe[0]);

  d_assert(strcmp(buf, "ok") == 0);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;

  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ | PROT_WRITE);
  hard_assert(r == 0);

  runtime<PageSize>().unlock();
  runtime<PageSize>().heap().unlock();
}

template <size_t PageSize>
void MeshableArena<PageSize>::doAfterForkChild() {
  afterForkChild();
}

template <size_t PageSize>
void MeshableArena<PageSize>::afterForkChild() {
  runtime<PageSize>().updatePid();

  if (!kMeshingEnabled) {
    return;
  }

  if (_forkPipe[0] == -1) {
    return;
  }

  internal::Heap().unlock();
  runtime<PageSize>().unlock();
  runtime<PageSize>().heap().unlock();

  close(_forkPipe[0]);

  char *oldSpanDir = _spanDir;

  int newFd = openSpanFile(kArenaSize);

  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(newFd, &fileinfo);
  d_assert(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == kArenaSize);

  const int oldFd = _fd;

  const auto bitmap = allocatedBitmap();
  for (auto const &i : bitmap) {
    int result = internal::copyFile(newFd, oldFd, i << kPageShift, PageSize);
    d_assert(result == CPUInfo::PageSize);
  }

  int r = mprotect(_arenaBegin, kArenaSize, PROT_READ | PROT_WRITE);
  hard_assert(r == 0);

  void *ptr = mmap(_arenaBegin, kArenaSize, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, newFd, 0);
  hard_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

  {
    internal::unordered_set<void *> seenMiniheaps{};

    for (auto const &i : _meshedBitmap) {
      void *mh_void = miniheapForArenaOffset(i);
      if (seenMiniheaps.find(mh_void) != seenMiniheaps.end()) {
        continue;
      }
      seenMiniheaps.insert(mh_void);

      auto *mh = reinterpret_cast<MiniHeap<PageSize> *>(mh_void);
      const auto meshCount = mh->meshCount();
      d_assert(meshCount > 1);

      const auto sz = mh->spanSize();
      const auto keep = reinterpret_cast<void *>(mh->getSpanStart(arenaBegin()));
      const auto keepOff = offsetFor(keep);

      mh->forEachMeshed([&](const MiniHeap<PageSize> *meshedMh) {
        if (!meshedMh->isMeshed())
          return false;

        const auto remove = reinterpret_cast<void *>(meshedMh->getSpanStart(arenaBegin()));
        const auto removeOff = offsetFor(remove);

#ifndef NDEBUG
        const Length pageCount = sz >> kPageShift;
        for (size_t j = 0; j < pageCount; j++) {
          d_assert(_mhIndex[removeOff + j].load().value() == _mhIndex[keepOff].load().value());
        }
#endif

        void *remapPtr =
            mmap(remove, sz, HL_MMAP_PROTECTION_MASK, kMapShared | MAP_FIXED, newFd, keepOff << kPageShift);
        hard_assert_msg(remapPtr != MAP_FAILED, "mesh remap failed: %d", errno);

        return false;
      });
    }
  }

  _fd = newFd;

  internal::Heap().free(oldSpanDir);

  close(oldFd);

  while (write(_forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
  }
  close(_forkPipe[1]);

  _forkPipe[0] = -1;
  _forkPipe[1] = -1;
}

template class MeshableArena<kPageSize4K>;
template class MeshableArena<kPageSize16K>;
}  // namespace mesh
