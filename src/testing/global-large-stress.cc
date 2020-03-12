#include <cstdio>
#include <cstdlib>
#include <vector>

static constexpr size_t kMinAllocSz = 800000;
static constexpr size_t kMaxAllocSz = 900000;
static constexpr unsigned kMaxLiveAlloc = 128;  // keep no more than 128 * kMaxAllocSz memory allocated.

int main(void) {
  std::vector<void *> alloc(kMaxLiveAlloc, nullptr);
  for (size_t i = 0; i < kMaxLiveAlloc; i++) {
    if (alloc[i] != nullptr) {
      fprintf(stderr, "alloc not zero initialized!\n");
    }
  }

  while (1) {
    const size_t ix = rand() % kMaxLiveAlloc;
    const size_t sz = (rand() % (kMaxAllocSz - kMinAllocSz)) + kMinAllocSz;

    free(alloc[ix]);
    alloc[ix] = malloc(sz);
  }
  return 0;
}
