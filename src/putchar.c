// -*- mode: c; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

// Implementation of _putchar required by printf.c

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

void _putchar(char c) {
#ifdef _WIN32
  HANDLE hStderr = GetStdHandle(STD_ERROR_HANDLE);
  DWORD written;
  WriteFile(hStderr, &c, 1, &written, NULL);
#else
  ssize_t ret = write(STDERR_FILENO, &c, 1);
  (void)ret;
#endif
}
