// Copyright 2013 Bobby Powers. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#define _GNU_SOURCE  // asprintf
#include <stdio.h>   // for NULL, fprintf, snprintf, stderr, BUFSIZ, fclose, etc
#undef _GNU_SOURCE

#include <ctype.h>    // for isdigit
#include <dirent.h>   // for closedir, readdir, dirent, opendir, rewinddir, etc
#include <errno.h>    // for errno
#include <fcntl.h>    // for O_RDONLY
#include <libgen.h>   // for basename
#include <stdarg.h>   // for va_list
#include <stdbool.h>  // for bool
#include <stdlib.h>   // for free, calloc, atoi, exit, malloc, realloc, etc
#include <string.h>   // for strcmp, strdup, strncmp, strncpy
#include <unistd.h>   // for ssize_t, geteuid

#include "measure_rss.h"

#define NOINLINE __attribute__((noinline))
#define COMM_MAX 16
#define CMD_DISPLAY_MAX 32
#define PAGE_SIZE 4096
#define MAP_DETAIL_LEN sizeof("Size:                  4 kB") - 1
#define MAP_DETAIL_OFF 16

#define LINE_BUF_SIZE 512

#define TY_NONLINEAR "Nonlinear:"
#define LEN_NONLINEAR sizeof(TY_NONLINEAR) - 1
#define TY_VM_FLAGS "VmFlags:"
#define LEN_VM_FLAGS sizeof(TY_VM_FLAGS) - 1

#define OFF_NAME 73

static void die(const char *, ...);

static char *_readlink(char *);

static CmdInfo *cmdinfo_new(int, size_t, char *);
static void cmdinfo_free(CmdInfo *);

static int smap_read_int(char *, int);
static size_t smap_details_len(void);

static int proc_name(CmdInfo *, int);
static int proc_mem(CmdInfo *, int, size_t, char *);
static int proc_cmdline(int, char *buf, size_t len);


void __attribute__((noreturn)) die(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  exit(EXIT_FAILURE);
}

/// _readlink is a simple wrapper around readlink which returns a
/// malloc'ed buffer that the caller now owns, containing the
/// null-terminated contents of the symbolic link.
char *_readlink(char *path) {
  ssize_t n;
  char *b;

  for (int len = 64;; len *= 2) {
    b = malloc(len);
    if (!b)
      return NULL;

    n = readlink(path, b, len);
    if (n == -1) {
      free(b);
      return NULL;
    }
    if (n < len) {
      b[n] = '\0';
      return b;
    }
    free(b);
  }
}

CmdInfo *cmdinfo_new(int pid, size_t details_len, char *details_buf) {
  int err;
  CmdInfo *ci;

  ci = calloc(sizeof(CmdInfo), 1);
  if (!ci)
    goto error;
  ci->npids = 1;

  err = proc_name(ci, pid);
  if (err)
    goto error;

  err = proc_mem(ci, pid, details_len, details_buf);
  if (err)
    goto error;

  return ci;
error:
  cmdinfo_free(ci);
  return NULL;
}

void cmdinfo_free(CmdInfo *ci) {
  if (ci && ci->name)
    free(ci->name);
  free(ci);
}

int smap_read_int(char *chunk, int line) {
  // line is 1-indexed, but offsets into the smap are 0-index.
  return atoi(&chunk[(line - 1) * MAP_DETAIL_LEN + MAP_DETAIL_OFF]);
}

int proc_name(CmdInfo *ci, int pid) {
  int n;
  char path[32], buf[BUFSIZ], shortbuf[COMM_MAX + 1];

  snprintf(path, sizeof(path), "/proc/%d/exe", pid);

  char *p = _readlink(path);
  // we can't read the exe in 2 cases: the process has exited,
  // or it refers to a kernel thread.  Either way, we don't want
  // to gather info on it.
  if (!p)
    return -1;

  n = proc_cmdline(pid, buf, BUFSIZ);
  if (n <= 0) {
    free(p);
    return -1;
  }

  strncpy(shortbuf, buf, COMM_MAX);
  shortbuf[COMM_MAX] = '\0';

  // XXX: I thnk this is wrong.  a) we can use strncmp rather
  // than the copy.  b) >= doesn/t make sense, it should be !=.
  if (strcmp(p, shortbuf) >= 0)
    ci->name = strdup(basename(p));
  else
    ci->name = strdup(buf);
  free(p);
  return 0;
}

int proc_cmdline(int pid, char *buf, size_t len) {
  int fd;
  char path[32];
  ssize_t n;

  snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

  fd = open(path, O_RDONLY);
  if (fd < 0)
    return -1;
  n = read(fd, buf, len - 1);
  if (n < 0)
    die("proc_cmdline(%d): read failed: %s", pid, strerror(errno));
  if (!n)
    goto error;

  buf[n] = '\0';

  close(fd);
  return n;
error:
  close(fd);
  return -1;
}

size_t smap_details_len(void) {
  size_t len = 0, result = 0;
  FILE *f = NULL;
  char *ok;
  char line[LINE_BUF_SIZE];

  f = fopen("/proc/self/smaps", "r");
  if (!f)
    return 0;

  // skip first line, which is the name + address of the mapping
  ok = fgets(line, LINE_BUF_SIZE, f);
  if (!ok)
    goto out;

  while (true) {
    ok = fgets(line, LINE_BUF_SIZE, f);
    if (!ok)
      goto out;
    else if (strncmp(line, TY_NONLINEAR, LEN_NONLINEAR) == 0)
      break;
    else if (strncmp(line, TY_VM_FLAGS, LEN_VM_FLAGS) == 0)
      break;
    if (strlen(line) != MAP_DETAIL_LEN + 1)
      die("unexpected line len %zu != %zu", strlen(line), MAP_DETAIL_LEN + 1);
    len += MAP_DETAIL_LEN + 1;
  }
  result = len;

out:
  fclose(f);
  return result;
}

int proc_mem(CmdInfo *ci, int pid, size_t details_len, char *details_buf) {
  float priv;
  FILE *f;
  size_t kb, kb2;
  bool skip_read;
  char path[32];

  snprintf(path, sizeof(path), "/proc/%d/smaps", pid);
  priv = 0;
  skip_read = false;
  f = fopen(path, "r");
  if (!f)
    return -1;

  while (true) {
    size_t len;
    bool is_heap = false;
    float m;
    char *ok;
    char *line = details_buf;

    if (skip_read)
      ok = line;
    else
      ok = fgets(line, details_len + 1, f);
    skip_read = false;

    if (!ok)
      break;

    // first line is VMA info - if not anonymous, the name
    // of the file/section is at offset OFF_NAME (73)
    len = strlen(line);
    if (len > OFF_NAME)
      is_heap = strncmp(&line[OFF_NAME], "[heap]", 6) == 0;
    if (!len)
      break;
    //*strstr(&line[OFF_NAME], "\n") = '\0';
    //printf("NAME: %50s: ", &line[OFF_NAME]);

    // memset(line, 0, details_len+1);
    len = fread(line, 1, details_len, f);
    if (len != details_len)
      die("couldn't read details of %d (%zu != %zu) - out of sync?:\n%s", pid, len, details_len, line);
    line[details_len] = '\0';

    // Rss - line 2
    kb = smap_read_int(line, 4);
    ci->rss += kb;
    // Pss - line 3
    kb2 = smap_read_int(line, 5);
    if (kb < kb2)
      fprintf(stderr, "ERROR\n");
    // fprintf(stderr, "\t\t\t%zu/%f\n", kb, m);
    m = kb2;
    ci->pss += m;
    // we don't need PSS_ADJUST for heap because
    // the heap is private and anonymous.
    if (is_heap)
      ci->heap += m;

    // Shared_Dirty, is meshed
    kb = smap_read_int(line, 7);
    ci->dirty += kb;

    // Private_Clean & Private_Dirty are lines 6 and 7
    priv += smap_read_int(line, 8);
    kb = smap_read_int(line, 9);
    ci->dirty += kb;
    priv += kb;

    ci->swap += smap_read_int(line, 13);

    // after the constant-sized smap details, there is an
    // optional Nonlinear line, followed by the final
    // VmFlags line.
    while (true) {
      ok = fgets(line, details_len, f);
      if (!ok) {
        goto end;
      } else if (strncmp(line, TY_VM_FLAGS, LEN_VM_FLAGS) == 0) {
        break;
      } else if (strlen(line) > MAP_DETAIL_LEN) {
        // older kernels don't have VmFlags,
        // but can have Nonlinear.  If we have
        // a line longer than a detail line it
        // is the VMA info and we should skip
        // reading the VMA info at the start
        // of the next loop iteration.
        skip_read = true;
        break;
      }
    }
  }
end:
  ci->shared = ci->pss - priv;

  fclose(f);
  return 0;
}

int get_self_rss(CmdInfo *ci) {
  size_t details_len;
  char *details_buf;
  CmdInfo *ci2;

  details_len = smap_details_len();
  details_buf = malloc(details_len + 1);
  if (!details_buf)
    die("malloc(details_len+1 failed\n");
  details_buf[details_len] = 0;

  ci2 = cmdinfo_new(getpid(), details_len, details_buf);
  if (!ci2)
    die("cmdinfo_new failed\n");
  *ci = *ci2;

  free(ci2);
  free(details_buf);

  return 0;
}

void print_self_rss(void) {
  CmdInfo ci;

  memset(&ci, 0, sizeof(ci));

  if (get_self_rss(&ci) != 0)
    die("self_rss failed\n");

  // printf("\tRss: %10.3f\tPss: %10.3f\tDirty: %10.3f\n", ci->rss/1024.0, ci->pss/1024.0, ci->dirty/1024.0);
  printf("\tRSS:\t%10.3f MB\n", ci.pss / 1024.0);
}
