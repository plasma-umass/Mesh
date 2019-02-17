// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "measure_rss.h"

extern "C" int get_rss_kb() {
  constexpr size_t bufLen = 4096;
  char buf[bufLen];

  memset(buf, 0, bufLen);

  int fd = open("/proc/self/status", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;

  ssize_t bytesRead = read(fd, buf, bufLen - 1);
  close(fd);

  if (bytesRead == -1)
    return -1;

  for (char *line = buf; line != nullptr && *line != 0; line = strchr(line, '\n')) {
    if (*line == '\n')
      line++;
    if (strncmp(line, "VmRSS:", strlen("VmRSS:")) != 0) {
      continue;
    }

    char *rssString = line + strlen("VmRSS:");
    return atoi(rssString);
  }

  return -1;
}
