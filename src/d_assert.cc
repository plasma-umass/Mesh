// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <cstdarg>
#include <cstdlib>  // for abort

#if defined(_WIN32)
#include <io.h>
#define write _write
#define STDERR_FILENO 2
#else
#include <unistd.h>
#endif

#include "common.h"

// On Windows, we don't need the asprintf/vasprintf fallbacks
// (they try to call vasprintf which doesn't exist on Windows)
#if defined(_WIN32)
#define HAVE_ASPRINTF 1
#define HAVE_VASPRINTF 1
#endif

#include "rpl_printf.c"

// mutex protecting debug and __mesh_assert_fail to avoid concurrent
// use of static buffers by multiple threads
inline static mutex *getAssertMutex(void) {
  static char assertBuf[sizeof(std::mutex)];
  static mutex *assertMutex = new (assertBuf) mutex();

  return assertMutex;
}

// threadsafe printf-like debug statements safe for use in an
// allocator (it will never call into malloc or free to allocate
// memory)
void mesh::debug(const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  static char buf[buf_len];
  std::lock_guard<std::mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  int len = rpl_vsnprintf(buf, buf_len - 1, fmt, args);
  va_end(args);

  buf[buf_len - 1] = 0;
  if (len > 0) {
    auto result ATTRIBUTE_UNUSED = write(STDERR_FILENO, buf, len);
    // ensure a trailing newline is written out
    if (buf[len - 1] != '\n')
      result = write(STDERR_FILENO, "\n", 1);
    (void)result;  // suppress unused variable warning
  }
}

// out-of-line function called to report an error and exit the program
// when an assertion failed.
void mesh::internal::__mesh_assert_fail(const char *assertion, const char *file, const char *func, int line,
                                        const char *fmt, ...) {
  constexpr size_t buf_len = 4096;
  constexpr size_t usr_len = 512;
  static char buf[buf_len];
  static char usr[usr_len];
  std::lock_guard<std::mutex> lock(*getAssertMutex());

  va_list args;

  va_start(args, fmt);
  (void)rpl_vsnprintf(usr, usr_len - 1, fmt, args);
  va_end(args);

  usr[usr_len - 1] = 0;

  int len = rpl_snprintf(buf, buf_len - 1, "%s:%d:%s: ASSERTION '%s' FAILED: %s\n", file, line, func, assertion, usr);
  if (len > 0) {
    auto result ATTRIBUTE_UNUSED = write(STDERR_FILENO, buf, len);
    (void)result;  // suppress unused variable warning
  }

  // void *array[32];
  // size_t size = backtrace(array, 10);
  // backtrace_symbols_fd(array, size, STDERR_FILENO);

  abort();
}
