// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#elif defined(__APPLE__)
#include <mach/task.h>
#include <mach/mach_init.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#endif
#include <unistd.h>

#include "measure_rss.h"

extern "C" int get_rss_kb() {
#if defined(__linux__)
  constexpr size_t bufLen = 4096;
  char buf[bufLen] = {0};

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
#elif defined(__APPLE__)
  struct task_basic_info info;
  task_t curtask = MACH_PORT_NULL;
  mach_msg_type_number_t cnt = TASK_BASIC_INFO_COUNT;
  int pid = getpid();

  if (task_for_pid(current_task(), pid, &curtask) != KERN_SUCCESS)
    return -1;

  if (task_info(curtask, TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &cnt) != KERN_SUCCESS)
    return -1;

  return static_cast<int>(info.resident_size);
#elif defined(__FreeBSD__)
  struct kinfo_proc info;
  size_t len = sizeof(info);
  int pid = getpid();
  int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};

  if (sysctl(mib, 4, &info, &len, nullptr, 0) != 0)
    return -1;

  return static_cast<int>(info.ki_rssize);
#endif
}
