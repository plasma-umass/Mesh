// -*- mode: c++ -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure

  Copyright (C) 2000-2014 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef HL_FILE_BACKED_MMAPHEAP_H
#define HL_FILE_BACKED_MMAPHEAP_H

#if defined(_WIN32)
#error "TODO"
#include <windows.h>
#else
// UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
#include <copyfile.h>
#else
#include <sys/sendfile.h>
#endif

#include <new>

#include "internal.h"

#ifndef HL_MMAP_PROTECTION_MASK
#error "define HL_MMAP_PROTECTION_MASK before including heaplayers.h"
#endif

#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

/**
 * @class FileBackedMmapHeap
 * @brief Modified MmapHeap for use in Mesh.
 */

static void *fbmmInstance;

namespace mesh {

namespace internal {
static const char *const TMP_DIRS[] = {
    "/dev/shm", "/tmp",
};

int copyfile(int dstFd, int srcFd, size_t sz) {
#if defined(__APPLE__) || defined(__FreeBSD__)
  // fcopyfile works on FreeBSD and OS X 10.5+
  int result = fcopyfile(srcFd, dstFd, 0, COPYFILE_ALL);
#else
  // sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
  off_t bytesCopied = 0;
  struct stat fileinfo;
  memset(&fileinfo, 0, sizeof(fileinfo));
  fstat(srcFd, &fileinfo);
  d_assert_msg(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == sz,
               "copyfile: expected %zu == %zu", fileinfo.st_size, sz);
  int result = sendfile(dstFd, srcFd, &bytesCopied, sz);
  // on success, ensure the entire results were copied
  if (result == 0)
    d_assert(bytesCopied == fileinfo.st_size);
#endif

  return result;
}
}

class MmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MmapHeap);

public:
  enum { Alignment = MmapWrapper::Alignment };

  MmapHeap() {
  }

  inline void *map(size_t sz, int flags, int fd) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    void *ptr = mmap(nullptr, sz, HL_MMAP_PROTECTION_MASK, flags, fd, 0);
    if (ptr == MAP_FAILED)
      abort();

    d_assert(_vmaMap.find(ptr) == _vmaMap.end());
    _vmaMap[ptr] = sz;
    d_assert(_vmaMap.find(ptr) != _vmaMap.end());
    d_assert(_vmaMap[ptr] == sz);

    d_assert(reinterpret_cast<size_t>(ptr) % Alignment == 0);

    return ptr;
  }

  inline void *malloc(size_t sz) {
    return map(sz, MAP_PRIVATE | MAP_ANONYMOUS, -1);
  }

  inline size_t getSize(void *ptr) {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("mmap: invalid getSize: %p", ptr);
      return 0;
    }
    return entry->second;
  }

  inline void free(void *ptr) {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("mmap: invalid free: %p", ptr);
      return;
    }

    auto sz = entry->second;

    munmap(ptr, sz);
    //madvise(ptr, sz, MADV_DONTNEED);
    //mprotect(ptr, sz, PROT_NONE);

    _vmaMap.erase(entry);
    d_assert(_vmaMap.find(ptr) == _vmaMap.end());
  }

protected:
  internal::unordered_map<void *, size_t> _vmaMap{};
};

class FileBackedMmapHeap : public MmapHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(FileBackedMmapHeap);
  typedef MmapHeap SuperHeap;

public:
  enum { Alignment = MmapWrapper::Alignment };

  FileBackedMmapHeap() : SuperHeap() {
    d_assert(fbmmInstance == nullptr);
    fbmmInstance = this;

    // check if we have memfd_create, if so use it.

    _spanDir = openSpanDir(getpid());
    d_assert(_spanDir != nullptr);

    on_exit(staticOnExit, this);
    pthread_atfork(staticPrepareForFork, staticAfterForkParent, staticAfterForkChild);
  }

  inline void *malloc(size_t sz) {
    if (sz == 0)
      return nullptr;

    // Round up to the size of a page.
    sz = (sz + CPUInfo::PageSize - 1) & (size_t) ~(CPUInfo::PageSize - 1);

    int fd = openSpanFile(sz);
    void *ptr = SuperHeap::map(sz, MAP_SHARED, fd);

    _fdMap[ptr] = fd;

    return ptr;
  }

  inline void free(void *ptr) {
    auto entry = _vmaMap.find(ptr);
    if (unlikely(entry == _vmaMap.end())) {
      debug("fb-mmap: invalid free: %p", ptr);
      return;
    }

    auto sz = entry->second;

    munmap(ptr, sz);
    //madvise(ptr, sz, MADV_DONTNEED);
    //mprotect(ptr, sz, PROT_NONE);

    _vmaMap.erase(entry);
    d_assert(_vmaMap.find(ptr) == _vmaMap.end());

    auto fdEntry = _fdMap.find(ptr);
    d_assert(fdEntry != _fdMap.end());

    int result = fallocate(fdEntry->second, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, sz);
    d_assert(result == 0);

    close(fdEntry->second);
    _fdMap.erase(fdEntry);
  }

  void mesh(void *keep, void *remove) {
  }

protected:
  int openSpanFile(size_t sz) {
    constexpr size_t buf_len = 64;
    char buf[buf_len];
    memset(buf, 0, buf_len);

    d_assert(_spanDir != nullptr);
    sprintf(buf, "%s/XXXXXX", _spanDir);

    int fd = mkstemp(buf);
    if (fd < 0) {
      debug("mkstemp: %d\n", errno);
      abort();
    }

    // we only need the file descriptors, not the path to the file in the FS
    int err = unlink(buf);
    if (err != 0) {
      debug("unlink: %d\n", errno);
      abort();
    }

    // TODO: see if fallocate makes any difference in performance
    err = ftruncate(fd, sz);
    if (err != 0) {
      debug("ftruncate: %d\n", errno);
      abort();
    }

    // if a new process gets exec'ed, ensure our heap is completely freed.
    err = fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (err != 0) {
      debug("fcntl: %d\n", errno);
      abort();
    }

    return fd;
  }

  char *openSpanDir(int pid) {
    constexpr size_t buf_len = 64;

    for (auto tmpDir : internal::TMP_DIRS) {
      char buf[buf_len];
      memset(buf, 0, buf_len);

      snprintf(buf, buf_len - 1, "%s/alloc-mesh-%d", tmpDir, pid);
      int result = mkdir(buf, 0755);
      // we will get EEXIST if we have re-execed
      if (result != 0 && errno != EEXIST)
        continue;

      char *spanDir = reinterpret_cast<char *>(internal::Heap().malloc(strlen(buf)+1));
      strcpy(spanDir, buf);
      return spanDir;
    }

    return nullptr;
  }

  static void staticOnExit(int code, void *data) {
    reinterpret_cast<FileBackedMmapHeap *>(data)->exit();
  }

  static void staticPrepareForFork() {
    d_assert(fbmmInstance != nullptr);
    reinterpret_cast<FileBackedMmapHeap *>(fbmmInstance)->prepareForFork();
  }

  static void staticAfterForkParent() {
    d_assert(fbmmInstance != nullptr);
    reinterpret_cast<FileBackedMmapHeap *>(fbmmInstance)->afterForkParent();
  }

  static void staticAfterForkChild() {
    d_assert(fbmmInstance != nullptr);
    reinterpret_cast<FileBackedMmapHeap *>(fbmmInstance)->afterForkChild();
  }

  void exit() {
    rmdir(_spanDir);
  }

  void prepareForFork() {
    xxmalloc_lock();
    int err = pipe(_forkPipe);
    if (err == -1)
      abort();
  }

  void afterForkParent() {
    xxmalloc_unlock();
    close(_forkPipe[1]);
    char buf[8];
    memset(buf, 0, 8);

    // wait for our child to close + reopen memory.  Without this
    // fence, we may experience memory corruption?

    while (read(_forkPipe[0], buf, 4) == EAGAIN) {
    }
    close(_forkPipe[0]);

    _forkPipe[0] = -1;
    _forkPipe[1] = -1;

    d_assert(strcmp(buf, "ok") == 0);
  }

  void afterForkChild() {
    xxmalloc_unlock();
    close(_forkPipe[0]);

    char *oldSpanDir = _spanDir;

    // update our pid + spanDir
    _spanDir = openSpanDir(getpid());
    d_assert(_spanDir != nullptr);

    // open new files for all open spans
    for (auto entry : _fdMap) {
      size_t sz = _vmaMap[entry.first];
      d_assert(sz != 0);

      int newFd = openSpanFile(sz);

      struct stat fileinfo;
      memset(&fileinfo, 0, sizeof(fileinfo));
      fstat(newFd, &fileinfo);
      d_assert(fileinfo.st_size >= 0 && (size_t)fileinfo.st_size == sz);

      internal::copyfile(newFd, entry.second, sz);

      // remap the new region over the old
      void *ptr = mmap(entry.first, sz, HL_MMAP_PROTECTION_MASK, MAP_SHARED | MAP_FIXED, newFd, 0);
      d_assert_msg(ptr != MAP_FAILED, "map failed: %d", errno);

      close(entry.second);
      _fdMap[entry.first] = newFd;
    }

    internal::Heap().free(oldSpanDir);

    while (write(_forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
    }
    close(_forkPipe[1]);

    _forkPipe[0] = -1;
    _forkPipe[1] = -1;
  }

  internal::unordered_map<void *, int> _fdMap{};
  int _forkPipe[2]{-1, -1};  // used for signaling during fork
  char *_spanDir{nullptr};
};
}

#endif  // HL_FILE_BACKED_MMAPHEAP_H
