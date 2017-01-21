// Copyright 2016 University of Massachusetts, Amherst

#include <stdio.h>   // for sprintf
#include <stdlib.h>  // for abort
#include <unistd.h>  // for write
#include <cstddef>   // for size_t
#include <new>       // for operator new
#include <random>

// never allocate exeecutable heap
#define HL_MMAP_PROTECTION_MASK (PROT_READ | PROT_WRITE)

#define MALLOC_TRACE 0

#include "bitmap.h"
#include "heaplayers.h"
#include "rng/mwc.h"

#include "file-backed-mmap.h"

#include "wrappers/gnuwrapper.cpp"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

using namespace HL;

// allocator for mesh-internal data structures, like heap metadata
typedef ExactlyOneHeap<LockedHeap<PosixLockType, FreelistHeap<BumpAlloc<16384, PrivateFileBackedMmapHeap>>>>
    InternalAlloc;

class TopHeap : public ExactlyOneHeap<FileBackedMmapHeap<InternalAlloc>> {
public:
  typedef ExactlyOneHeap<FileBackedMmapHeap<InternalAlloc>> Super;
};

static void
debug(const char *fmt, ...)
{
  static char buf[256];
  va_list args;

	va_start(args, fmt);
	int len = vsnprintf(buf, 255, fmt, args);
	va_end(args);

  buf[255] = 0;
  if (len > 0)
    write(STDERR_FILENO, buf, len);
}


thread_local static std::random_device RD;

template <typename SuperHeap,
          typename InternalAlloc,
          size_t PageSize = 4096,
          size_t MinObjectSize = 16,
          size_t MaxObjectSize = 2048,
          size_t MinAvailablePages = 4,
          size_t SpanSize = 128UL * 1024UL, // 128Kb spans
          unsigned int FullNumerator = 3,
          unsigned int FullDenominator = 4>
class MiniHeap : public SuperHeap {
public:
  enum { Alignment = (int)MinObjectSize };

  MiniHeap(size_t objectSize)
    : _objectSize(objectSize), _objectCount(), _inUseCount(),
      _fullCount(), _rng(RD(), RD()), _bitmap() {

    _span = SuperHeap::malloc(SpanSize);
    if (!_span)
      abort();

    constexpr auto heapPages = SpanSize / PageSize;
    _objectCount = SpanSize / objectSize;
    _fullCount = FullNumerator * _objectCount / FullDenominator;

    debug("MiniHeap(%zu): reserving %zu objects on %zu pages (%u/%u full: %zu)\n",
          objectSize, _objectCount, heapPages, FullNumerator, FullDenominator, _fullCount);

    _bitmap.reserve(_objectCount);
  }

  inline void *malloc(size_t sz) {
    assert(sz == _objectSize);

    // should never have been called
    if (isFull())
      abort();

    while (true) {
      auto random = _rng.next() % _objectCount;

      if (_bitmap.tryToSet(random)) {
        auto ptr = reinterpret_cast<void *>((uintptr_t)_span + random * _objectSize);
        _inUseCount++;
        return ptr;
      }
    }
  }

  inline void free(void *ptr) {
  }

  inline size_t getSize(void *ptr) {
    auto ptrval = (uintptr_t)ptr;
    if (ptrval < (uintptr_t)_span || ptrval >= (uintptr_t)_span + SpanSize)
      return 0;

    return _objectSize;
  }

  inline bool isFull() {
    return _inUseCount >= _fullCount;
  }

  void *_span;
  size_t _objectSize;
  size_t _objectCount;
  size_t _inUseCount;
  size_t _fullCount;
  MWC _rng;
  BitMap<InternalAlloc> _bitmap;
};

template <typename SuperHeap, typename InternalAlloc>
class MeshingHeap {
public:
  enum { Alignment = 16 };  // FIXME

  MeshingHeap() : _current(nullptr), _alloc() {
  }

  inline void *malloc(size_t sz) {
    if (unlikely(_current == nullptr)) {
      void *buf = _alloc.malloc(sizeof(MiniHeap<SuperHeap, InternalAlloc>));
      if (!buf)
        abort();
      _current = new (buf) MiniHeap<SuperHeap, InternalAlloc>(sz);
    }

    void *ptr = _current->malloc(sz);
    if (_current->isFull()) {
      _current = nullptr;
    }

    return ptr;
  }

  inline void free(void *ptr) {
    // FIXME: check if ptr is in current, if so free there, otherwise check all other miniheaps
    // this needs to take into account threads + locks, maybe
  }

  inline size_t getSize(void *ptr) {
    if (_current == nullptr)
      return 0;
    return _current->getSize(ptr);
  }

private:
  MiniHeap<SuperHeap, InternalAlloc> *_current;
  InternalAlloc _alloc;
  // TODO: btree of address-ranges to miniheaps, for free
};


// fewer buckets
template <class PerClassHeap, class BigHeap>
class MiniKingsleyHeap :
   public StrictSegHeap<12,
                        Kingsley::size2Class,
                        Kingsley::class2Size,
                        PerClassHeap,
                        BigHeap> {};



// the mesh heap doesn't coalesce and doesn't have free lists
class CustomHeap : public ANSIWrapper<MiniKingsleyHeap<MeshingHeap<TopHeap, InternalAlloc>, TopHeap>> {};

inline static CustomHeap *getCustomHeap(void) {
  static char buf[sizeof(CustomHeap)];
  static CustomHeap *heap = new (buf) CustomHeap();
  return heap;
}

extern "C" {
void *xxmalloc(size_t sz) {
  return getCustomHeap()->malloc(sz);
}

void xxfree(void *ptr) {
  getCustomHeap()->free(ptr);
}

size_t xxmalloc_usable_size(void *ptr) {
  return getCustomHeap()->getSize(ptr);
}

void xxmalloc_lock() {
}

void xxmalloc_unlock() {
}
}
