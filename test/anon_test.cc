#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)

static int forkPipe[2];

static char *s1;
static char *s2;

static void afterForkParent() {
  char buf[8];
  memset(buf, 0, 8);

  while (read(forkPipe[0], buf, 4) == EAGAIN) {
  }

  forkPipe[0] = -1;
  forkPipe[1] = -1;

  assert(strcmp(buf, "ok") == 0);

  close(forkPipe[0]);
}

static void afterForkChild() {
  strcpy(s1, "b");

  while (write(forkPipe[1], "ok", strlen("ok")) == EAGAIN) {
  }
  close(forkPipe[1]);

  forkPipe[0] = -1;
  forkPipe[1] = -1;
}

int main() {
  int err = pipe(forkPipe);
  if (err == -1) {
    abort();
  }

  s1 = reinterpret_cast<char *>(
      mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  if (s1 == MAP_FAILED) {
    abort();
  }

  strcpy(s1, "a");

  int pid = fork();
  if (pid == -1) {
    abort();
  } else if (pid == 0) {
    afterForkChild();
  } else {
    afterForkParent();
    printf("s1: %s\n", s1);
  }

  return 0;
}
