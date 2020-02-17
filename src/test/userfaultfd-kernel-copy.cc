#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdarg>
#include <cstring>
#include <thread>

constexpr size_t kPageSize = 4096;
constexpr size_t kDataLen = 128;
constexpr size_t kArenaSize = kPageSize * 2;

void __attribute__((noreturn)) die(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);

  exit(EXIT_FAILURE);
}

void kernelCopy(int tmpFd, char *addr, char const *knownGood) {
  errno = 0;
  ssize_t len = read(tmpFd, addr, kDataLen);
  if (len != kDataLen) {
    fprintf(stderr, "read: %s (len: %zd)\n", strerror(errno), len);
    return;
  }

  if (memcmp(knownGood, addr, kDataLen) != 0) {
    die("data read from file doesn't match knownGood\n");
  }

  printf("read from file went as expected; data looks good.\n");
}

void setupPageTrap(int faultFd, char *arenaBegin, size_t pageOff, size_t len) {
  char *mappingBegin = arenaBegin + pageOff * kPageSize;

  // step 0: register the range with userfaultfd
  struct uffdio_register ufRegister = {};
  ufRegister.range.start = reinterpret_cast<unsigned long>(mappingBegin);
  ufRegister.range.len = len;
  ufRegister.mode = UFFDIO_REGISTER_MODE_WP;
  if (ioctl(faultFd, UFFDIO_REGISTER, &ufRegister) == -1) {
    die("ioctl(UFFDIO_REGISTER): %s\n", strerror(errno));
  }

  // step 1: use userfaultfd (rather than mprotect) to remap the pages read-only
  struct uffdio_writeprotect ufWriteProtect = {};
  ufWriteProtect.range.start = reinterpret_cast<unsigned long>(mappingBegin);
  ufWriteProtect.range.len = len;
  ufWriteProtect.mode = UFFDIO_WRITEPROTECT_MODE_WP;
  if (ioctl(faultFd, UFFDIO_WRITEPROTECT, &ufWriteProtect) == -1) {
    die("ioctl(UFFDIO_WRITEPROTECT): %s\n", strerror(errno));
  }
}

int main() {
  // get some random bytes + write it to a temp file
  char data[kDataLen] = {};
  if (getentropy(data, kDataLen) == -1) {
    die("getentropy: %s\n", strerror(errno));
  }

  char tmpFilename[] = "/tmp/userfaultfd-test-XXXXXX";
  int tmpFd = mkstemp(tmpFilename);
  if (tmpFd == -1) {
    die("mkstemp: %s\n", strerror(errno));
  }
  if (unlink(tmpFilename) == -1) {
    die("unlink: %s\n", strerror(errno));
  }

  size_t len = write(tmpFd, data, kDataLen);
  if (len < kDataLen) {
    die("write: partial write of %d\n", len);
  }
  if (lseek(tmpFd, 0, SEEK_SET) != 0) {
    die("lseek failed\n");
  }

  // the write-protection patchset doesn't yet support non-anonymous (or hugepage) VMAs
  char *arenaBegin = reinterpret_cast<char *>(
      mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
  if (arenaBegin == MAP_FAILED) {
    die("mmap: %s\n", strerror(errno));
  }

  // pre-fault things in; the write protection stuff below doesn't seem to work
  // well if the pages aren't already faulted in.  in a real allocator/GC the pages
  // we're dealing with will be faulted in so this seems fine.
  for (size_t i = 0; i < kArenaSize; i += kPageSize) {
    arenaBegin[i] = 0;
  }

  int faultFd = static_cast<int>(syscall(__NR_userfaultfd, O_CLOEXEC));
  if (faultFd == -1) {
    die("userfaultfd: %s\n", strerror(errno));
  }

  // the only feature we care about is write-protection faults -- this will fail
  // on a mainline kernel as the patchset isn't upstream yet.
  struct uffdio_api ufApi = {};
  ufApi.api = UFFD_API;
  ufApi.features = UFFD_FEATURE_PAGEFAULT_FLAG_WP;
  if (ioctl(faultFd, UFFDIO_API, &ufApi) == -1) {
    die("ioctl(UFFDIO_API): %s\n", strerror(errno));
  }
  if (ufApi.api != UFFD_API) {
    die("ioctl(UFFDIO_API) API version mismatch %lld != %lld\n", ufApi.api, UFFD_API);
  }

  // prefault in the entire arena to match the fact that the heap will already
  // be faulted in when meshing pages together
  for (size_t i = 0; i < kArenaSize; i += kPageSize) {
    arenaBegin[i] = 0;
  }

  // write-protect part of the arena
  setupPageTrap(faultFd, arenaBegin, 0, kPageSize);

  printf("part of the arena is read-only now and writes raise an event on our userfaultfd\n");

  // even works with kernelspace; +57 is to verify that non-page-aligned access work
  std::thread t1(kernelCopy, tmpFd, arenaBegin + 57, data);

  // now suspend until we receive an event from the kernelCopy thread
  struct uffd_msg msg = {};
  int nRead = read(faultFd, &msg, sizeof(msg));
  if (nRead == 0) {
    die("EOF on userfaultfd!\n");
  } else if (nRead == -1) {
    die("read: %s\n", strerror(errno));
  }

  if (msg.event != UFFD_EVENT_PAGEFAULT) {
    die("Unexpected event on userfaultfd\n");
  }

  printf("    UFFD_EVENT_PAGEFAULT event: ");
  printf("flags = %llx; ", msg.arg.pagefault.flags);
  printf("address = %llx\n", msg.arg.pagefault.address);

  sleep(2);

  printf("removing write protection and waking up other thread\n");

  // turn off write-protection; implicitly wakes the other thread up
  struct uffdio_writeprotect ufWriteProtect = {};
  ufWriteProtect.range.start = msg.arg.pagefault.address;
  ufWriteProtect.range.len = kPageSize;
  ufWriteProtect.mode = 0;
  if (ioctl(faultFd, UFFDIO_WRITEPROTECT, &ufWriteProtect) == -1) {
    die("ioctl(UFFDIO_WRITEPROTECT): %s\n", strerror(errno));
  }

  t1.join();
}
