// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINIHEAP_H
#define MESH__MINIHEAP_H

#include <pthread.h>

#include <atomic>
#include <random>

#include "bitmap.h"
#include "internal.h"

#include "rng/mwc.h"

#include "heaplayers.h"

#undef MESH_EXTRA_BITS

namespace mesh {

class MiniHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeap);

public:
  MiniHeap(void *span, size_t objectCount, size_t objectSize, size_t expectedSpanSize)
      : _bitmap(objectCount),
        _maxCount(objectCount),
        _objectSize(objectSize),
        _objectSizeReciprocal(1.0 / (float)objectSize),
        _spanSize(dynamicSpanSize()),
        _meshCount(1),
        _span{reinterpret_cast<char *>(span), 0, 0, 0}
#ifdef MESH_EXTRABITS
        ,
        _bitmap0(maxCount()),
        _bitmap1(maxCount()),
        _bitmap2(maxCount()),
        _bitmap3(maxCount())
#endif
  {
    if (!_span[0])
      abort();

    // debug("sizeof(MiniHeap): %zu", sizeof(MiniHeap));
    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), _objectSize);
    d_assert_msg(expectedSpanSize == dynamicSpanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), _objectSize);

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    d_assert_msg(objectSize == static_cast<size_t>(_objectSize), "%zu != %hu", objectSize, _objectSize);

    d_assert(_span[1] == nullptr);

    // dumpDebug();
  }

  ~MiniHeap() {
    _meshCount = ~0;
    // if (_meshCount > 1)
    //   dumpDebug();
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"mesh-count\": %d, \"bitmap\": \"%s\"}\n",
                this, objectSize(), maxCount(), meshCount(), _bitmap.to_string().c_str());
  }

  // should never be called directly, a freelist is populated from our bitmap
  // inline void *malloc();
  // inline void localFree(Freelist &freelist, MWC &prng, void *ptr);

  inline size_t free(void *ptr) {
    const ssize_t off = getOff(ptr);
    if (unlikely(off < 0))
      return _inUseCount;

    _bitmap.unset(off);
    const size_t prevInUse = _inUseCount.fetch_sub(1);
    // not strictly true, but good for tests
    d_assert(prevInUse - 1 == _inUseCount);
    return prevInUse - 1;
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
      // need to ensure we update the bitmap and in-use count
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
    return _maxCount;
  }

  inline size_t objectSize() const {
    return _objectSize;
  }

  inline size_t getSize(void *ptr) const {
    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span[0], ptr,
                 reinterpret_cast<uintptr_t>(_span[0]) + spanSize());

    return objectSize();
  }

  inline uintptr_t getSpanStart() const {
    return reinterpret_cast<uintptr_t>(_span[0]);
  }

  inline void incrementInUseCount(size_t additionalInUse) {
    _inUseCount += additionalInUse;
  }

  inline bool isEmpty() const {
    return _inUseCount == 0;
  }

  inline bool isFull() const {
    return _inUseCount == maxCount();
  }

  inline size_t inUseCount() const {
    return _inUseCount;
  }

  inline uint32_t refcount() const {
    return _refCount.load();
  }

  inline void ref() const {
    ++_refCount;
  }

  inline void unref() const {
    --_refCount;
  }

  inline bool isMeshingCandidate() const {
    return _refCount == 0 && objectSize() < kPageSize;
  }

  /// Returns the fraction full (in the range [0, 1]) that this miniheap is.
  inline double fullness() const {
    return static_cast<double>(_inUseCount) / static_cast<double>(maxCount());
  }

  const internal::Bitmap &bitmap() const {
    return _bitmap;
  }

  internal::Bitmap &writableBitmap() {
    return _bitmap;
  }

  void trackMeshedSpan(uintptr_t spanStart) {
    if (unlikely(_meshCount >= kMaxMeshes)) {
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

    return ptrFromOffset(off);
  }

  inline void *ptrFromOffset(size_t off) {
    return reinterpret_cast<void *>(getSpanStart() + off * _objectSize);
  }

  inline bool operator<(MiniHeap *&rhs) noexcept {
    return this->inUseCount() < rhs->inUseCount();
  }

  void dumpDebug() const {
    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = _inUseCount;
    const size_t meshCount = _meshCount;
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (inUse: %zu, mesh: %zu)\t%p-%p\n", this, _objectSize,
                maxCount(), heapPages, inUseCount, meshCount, _span[0],
                reinterpret_cast<uintptr_t>(_span[0]) + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string().c_str());
  }

  inline int bitmapGet(enum mesh::BitType type, void *ptr) const {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

#ifdef MESH_EXTRA_BITS
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
#endif
    d_assert(false);
    return -1;
  }

  inline int bitmapSet(enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

#ifdef MESH_EXTRA_BITS
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
#endif
    d_assert(false);
    return -1;
  }

  inline int bitmapClear(enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(ptr);
    d_assert(off >= 0);

#ifdef MESH_EXTRA_BITS
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
#endif
    d_assert(false);
    return -1;
  }

  inline ssize_t getOff(void *ptr) const {
    d_assert(getSize(ptr) == _objectSize);

    const auto span = spanStart(ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    // const size_t off = (ptrval - span) / _objectSize;
    const size_t off = (ptrval - span) * _objectSizeReciprocal;
    // hard_assert_msg(off == off2, "%zu != %zu", off, off2);
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

  internal::Bitmap _bitmap;               // 32 bytes 32
  internal::BinToken _token{};            // 8        40
  mutable atomic<uint32_t> _refCount{1};  // 4        44
  atomic<uint32_t> _inUseCount{0};        // 4        48
  const uint32_t _maxCount;               // 4        52
  const uint32_t _objectSize;             // 4        56
  const float _objectSizeReciprocal;      // 4        60
  const uint32_t _spanSize;               // 4        64 max 4 GB span size/allocation size, 56
  uint32_t _meshCount;                    // 4        68
  char *_span[kMaxMeshes];

#ifdef MESH_EXTRA_BITS
  internal::Bitmap _bitmap0;
  internal::Bitmap _bitmap1;
  internal::Bitmap _bitmap2;
  internal::Bitmap _bitmap3;
#endif
};

static_assert(sizeof(mesh::internal::Bitmap) == 32, "Bitmap too big!");
#ifdef MESH_EXTRA_BITS
static_assert(sizeof(MiniHeap) == 184, "MiniHeap too big!");
#else
static_assert(sizeof(MiniHeap) == 104, "MiniHeap too big!");
#endif
// static_assert(sizeof(MiniHeap) == 80, "MiniHeap too big!");
}  // namespace mesh

#endif  // MESH__MINIHEAP_H
