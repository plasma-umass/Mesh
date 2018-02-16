// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINIHEAP_H
#define MESH__MINIHEAP_H

#include <atomic>
#include <random>

#include "bitmap.h"
#include "freelist.h"
#include "internal.h"

#include "rng/mwc.h"

#include "heaplayers.h"

namespace mesh {

template <size_t MaxFreelistLen = sizeof(uint8_t) << 8,  // AKA max # of objects per miniheap
          size_t MaxMeshes = internal::MaxMeshes>        // maximum number of VM spans we can track
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);
  typedef MiniHeapBase<MaxFreelistLen, MaxMeshes> MiniHeap;

public:
  MiniHeapBase(void *span, size_t objectCount, size_t objectSize, mt19937_64 &prng, MWC &fastPrng,
               size_t expectedSpanSize)
      : _freelist{objectCount, prng, fastPrng},
        _objectSize(objectSize),
        _spanSize(dynamicSpanSize()),
        _span{reinterpret_cast<char *>(span)},
        _meshCount(1),
        _attached(true),
        _bitmap(maxCount()),
        _bitmap0(maxCount()),
        _bitmap1(maxCount()),
        _bitmap2(maxCount()),
        _bitmap3(maxCount()) {
    if (!_span[0])
      abort();

    //debug("sizeof(MiniHeap): %zu", sizeof(MiniHeap));
    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), _objectSize);
    d_assert_msg(expectedSpanSize == dynamicSpanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), _objectSize);

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    d_assert_msg(objectSize == static_cast<size_t>(_objectSize), "%zu != %hu", objectSize, _objectSize);

    d_assert(_span[1] == nullptr);

    // dumpDebug();
  }

  ~MiniHeapBase() {
    // if (_meshCount > 1)
    //   dumpDebug();
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"mesh-count\": %d, \"bitmap\": \"%s\"}\n", this, objectSize(),
                maxCount(), meshCount(), _bitmap.to_string().c_str());
  }

  // always "localMalloc"
  inline void *malloc(size_t sz) {
    if (unlikely(!_attached || isExhausted())) {
      dumpDebug();
      d_assert_msg(_attached && !isExhausted(), "attached: %d, full: %d", _attached.load(), isExhausted());
    }
    //d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    auto off = _freelist.pop();
    // mesh::debug("%p: ma %u", this, off);

    auto ptr = mallocAt(off);
    d_assert(ptr != nullptr);

    return ptr;
  }

  inline void localFree(void *ptr, mt19937_64 &prng, MWC &mwc) {
    const ssize_t freedOff = getOff(ptr);
    if (unlikely(freedOff < 0))
      return;

    _freelist.push(freedOff, prng, mwc);
    _bitmap.unset(freedOff);
    _inUseCount--;
  }

  inline void free(void *ptr) {
    const ssize_t off = getOff(ptr);
    if (unlikely(off < 0))
      return;

    _bitmap.unset(off);
    _inUseCount--;
  }

  /// Copies (for meshing) the contents of src into our span.
  inline void consume(const MiniHeap *src) {
    const auto srcSpan = src->getSpanStart();

    // this would be bad
    d_assert(src != this);

    // for each object in src, copy it to our backing span + update
    // our bitmap and in-use count
    for (auto const &off : src->bitmap()) {
      d_assert(!_bitmap.isSet(off));
      void *srcObject = reinterpret_cast<void *>(srcSpan + off * objectSize());
      void *dstObject = mallocAt(off);
      d_assert(dstObject != nullptr);
      memcpy(dstObject, srcObject, objectSize());
    }

    const auto srcSpans = src->spans();
    const auto srcMeshCount = src->meshCount();
    for (size_t i = 0; i < srcMeshCount; i++) {
      trackMeshedSpan(reinterpret_cast<uintptr_t>(srcSpans[i]));
    }
  }

  inline bool contains(void *ptr) const {
    return spanStart(ptr) != 0;
  }

  inline size_t spanSize() const {
    return _spanSize;
  }

  inline size_t dynamicSpanSize() const {
    size_t bytesNeeded = static_cast<size_t>(_objectSize) * maxCount();
    return mesh::RoundUpToPage(bytesNeeded);
  }

  inline size_t maxCount() const {
    return _freelist.maxCount();
  }

  inline size_t objectSize() const {
    return _objectSize;
  }

  inline size_t getSize(void *ptr) const {
    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span[0], ptr,
                 reinterpret_cast<uintptr_t>(_span[0]) + spanSize());

    return objectSize();
  }

  /// Whether we can still locally allocate out of this MiniHeap, or
  /// if our freelist has been exhausted.  This is NOT necessarily the
  /// same as whether our bitmap is full -- if an object is allocated
  /// on one thread, passed to another, and a non-local free happens
  /// from that other thread, we might end up with the case that our
  /// bitmap is non-full, but the freelist is empty (as non-local
  /// frees don't touch the bitmap).
  inline bool isExhausted() const {
    return _freelist.isExhausted();
  }

  inline uintptr_t getSpanStart() const {
    return reinterpret_cast<uintptr_t>(_span[0]);
  }

  inline void reattach(mt19937_64 &prng, MWC &fastPrng) {
    _freelist.init(prng, fastPrng, &_bitmap);
    _attached = true;
    d_assert(!isExhausted());
    // if (_meshCount > 1)
    //   mesh::debug("fixme? un-mesh when reattaching");
  }

  /// called when a LocalHeap is done with a MiniHeap (it is
  /// "detaching" it and releasing it back to the global heap)
  inline void detach() {
    _freelist.detach();
    _attached = false;
    atomic_thread_fence(memory_order_seq_cst);
  }

  inline bool isAttached() const {
    return _attached;
  }

  inline bool isEmpty() const {
    return _inUseCount == 0;
  }

  inline size_t inUseCount() const {
    return _inUseCount;
  }

  inline void ref() const {
    ++_refCount;
  }

  inline void unref() const {
    --_refCount;
  }

  inline bool isMeshingCandidate() const {
    if (_refCount > 0)
      mesh::debug("skipping due to MH reference (%zu)", _refCount);
    return !isAttached() && _refCount == 0 && objectSize() < 4096;
  }

  /// Returns the fraction full (in the range [0, 1]) that this miniheap is.
  inline double fullness() const {
    return static_cast<double>(_inUseCount) / static_cast<double>(maxCount());
  }

  const mesh::internal::Bitmap &bitmap() const {
    return _bitmap;
  }

  void trackMeshedSpan(uintptr_t spanStart) {
    if (_meshCount >= MaxMeshes) {
      mesh::debug("fatal: too many meshes for one miniheap");
      dumpDebug();
      abort();
    }

    _span[_meshCount] = reinterpret_cast<char *>(spanStart);
    _meshCount++;
  }

  size_t meshCount() const {
    return _meshCount;
  }

  char *const *spans() const {
    return _span;
  }

  internal::BinToken getBinToken() const {
    return _token;
  }

  void setBinToken(internal::BinToken token) {
    _token = token;
  }

  /// public for meshTest only
  inline void *mallocAt(size_t off) {
    if (!_bitmap.tryToSet(off)) {
      mesh::debug("%p: MA %u", this, off);
      dumpDebug();
      return nullptr;
    }

    _inUseCount++;

    return reinterpret_cast<void *>(getSpanStart() + off * _objectSize);
  }

  inline bool operator<(MiniHeap *&rhs) noexcept {
    return this->inUseCount() < rhs->inUseCount();
  }

  void dumpDebug() const {
    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = _inUseCount;
    const size_t meshCount = _meshCount;
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (full: %d, inUse: %zu, mesh: %zu)\t%p-%p\n", this,
                _objectSize, maxCount(), heapPages, this->isExhausted(), inUseCount, meshCount, _span[0],
                reinterpret_cast<uintptr_t>(_span[0]) + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string().c_str());
  }

  inline int bitmapGet(enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

    switch (type) {
    case MESH_BIT_0:
      return _bitmap0.isSet(off);
    case MESH_BIT_1:
      return _bitmap1.isSet(off);
    case MESH_BIT_2:
      return _bitmap2.isSet(off);
    case MESH_BIT_3:
      return _bitmap3.isSet(off);
    default:
      break;
    }
    d_assert(false);
    return -1;
  }

  inline int bitmapSet(enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

    switch (type) {
    case MESH_BIT_0:
      return _bitmap0.tryToSet(off);
    case MESH_BIT_1:
      return _bitmap1.tryToSet(off);
    case MESH_BIT_2:
      return _bitmap2.tryToSet(off);
    case MESH_BIT_3:
      return _bitmap3.tryToSet(off);
    default:
      break;
    }
    d_assert(false);
    return -1;
  }

  inline int bitmapClear(enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

    switch (type) {
    case MESH_BIT_0:
      return _bitmap0.unset(off);
    case MESH_BIT_1:
      return _bitmap1.unset(off);
    case MESH_BIT_2:
      return _bitmap2.unset(off);
    case MESH_BIT_3:
      return _bitmap3.unset(off);
    default:
      break;
    }
    d_assert(false);
    return -1;
  }

protected:
  inline uintptr_t spanStart(void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto len = _spanSize;

    // manually unroll loop once to capture the common case of
    // un-meshed miniheaps
    auto span = reinterpret_cast<uintptr_t>(_span[0]);
    if (likely(span <= ptrval && ptrval < span + len))
      return span;

    for (size_t i = 1; i < _meshCount; ++i) {
      if (unlikely(_span[i] == nullptr)) {
        mesh::debug("_span[%d] should be non-null (%zu)", i, _meshCount);
        dumpDebug();
        d_assert(false);
      }
      span = reinterpret_cast<uintptr_t>(_span[i]);
      if (span <= ptrval && ptrval < span + len)
        return span;
    }

    return 0;
  }

  inline ssize_t getOff(void *ptr) const {
    d_assert(getSize(ptr) == _objectSize);

    const auto span = spanStart(ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    const auto off = (ptrval - span) / _objectSize;
    // if (unlikely(span > ptrval || off >= maxCount())) {
    //   mesh::debug("MiniHeap(%p): invalid free of %p", this, ptr);
    //   return -1;
    // }

    // if (unlikely(!_bitmap.isSet(off))) {
    //   mesh::debug("MiniHeap(%p): double free of %p", this, ptr);
    //   dumpDebug();
    //   return -1;
    // }

    return off;
  }

  Freelist<MaxFreelistLen> _freelist;
  const uint32_t _objectSize;
  const uint32_t _spanSize;
  char *_span[MaxMeshes];
  internal::BinToken _token;

  atomic<uint32_t> _inUseCount{0}; // 60

  mutable uint32_t _refCount{0};
  uint32_t _meshCount;// : 7;
  atomic<uint32_t> _attached;// : 1;
  internal::Bitmap _bitmap; // 16 bytes
  internal::Bitmap _bitmap0; // 16 bytes
  internal::Bitmap _bitmap1; // 16 bytes
  internal::Bitmap _bitmap2; // 16 bytes
  internal::Bitmap _bitmap3; // 16 bytes
};

typedef MiniHeapBase<> MiniHeap;

static_assert(sizeof(mesh::internal::Bitmap) == 16, "Bitmap too big!");
//static_assert(sizeof(MiniHeap) == 96, "MiniHeap too big!");
static_assert(sizeof(MiniHeap) == 160, "MiniHeap too big!");
//static_assert(sizeof(MiniHeap) == 80, "MiniHeap too big!");

}  // namespace mesh

#endif  // MESH__MINIHEAP_H
