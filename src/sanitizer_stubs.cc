#include "sanitizer_common.h"

#include "sanitizer_allocator_internal.h"
#include "sanitizer_stacktrace_printer.h"

#include "internal.h"

namespace __sanitizer {

bool IsAllocatorOutOfMemory() {
  return false;
}

void *LowLevelAllocator::Allocate(size_t sz) {
  return mesh::internal::Heap().malloc(sz);
}

void InternalFree(void *ptr, InternalAllocatorCache *cache) {
  mesh::internal::Heap().free(ptr);
}

void *InternalAlloc(size_t sz, InternalAllocatorCache *cache, uptr alignment) {
  if (alignment == 0 || alignment <= mesh::internal::Heap().Alignment)
    return mesh::internal::Heap().malloc(sz);
  else
    d_assert(false);
}

void *PersistentAlloc(uptr sz) {
  return mesh::internal::Heap().malloc(sz);
}
}  // namespace __sanitizer
