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

class MiniHeap;

typedef void (*SpanCallback)(Span span, internal::PageType type);

class Flags {
private:
  DISALLOW_COPY_AND_ASSIGN(Flags);

  static inline constexpr uint32_t ATTRIBUTE_ALWAYS_INLINE getMask(uint32_t pos) {
    return 1UL << pos;
  }
  static constexpr uint32_t MeshedOffset = 0;
  static constexpr uint32_t AttachedOffset = 8;

public:
  explicit Flags() noexcept {
  }

  inline void setAttached() {
    set(AttachedOffset);
  }

  inline void unsetAttached() {
    unset(AttachedOffset);
  }

  inline bool isAttached() const {
    return is(AttachedOffset);
  }

  inline void setMeshed() {
    set(MeshedOffset);
  }

  inline void unsetMeshed() {
    unset(MeshedOffset);
  }

  inline bool isMeshed() const {
    return is(MeshedOffset);
  }

private:
  inline bool is(size_t offset) const {
    const auto mask = getMask(offset);
    return (_flags.load(std::memory_order_acquire) & mask) == mask;
  }

  inline void set(size_t offset) {
    const uint32_t mask = getMask(offset);

    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_flags,
                                                  &oldFlags,                  // old val
                                                  oldFlags | mask,            // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }
  }

  inline void unset(size_t offset) {
    const uint32_t mask = getMask(offset);

    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_flags,
                                                  &oldFlags,                  // old val
                                                  oldFlags & ~mask,           // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }
  }

  atomic_uint32_t _flags{0};
};

class MiniHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeap);

public:
  MiniHeap(void *arenaBegin, Span span, size_t objectCount, size_t objectSize)
      : _bitmap(objectCount),
        _span(span),
        _maxCount(objectCount),
        _objectSizeReciprocal(1.0 / (float)objectSize)
#ifdef MESH_EXTRABITS
        ,
        _bitmap0(maxCount()),
        _bitmap1(maxCount()),
        _bitmap2(maxCount()),
        _bitmap3(maxCount())
#endif
  {
    // debug("sizeof(MiniHeap): %zu", sizeof(MiniHeap));

    const auto expectedSpanSize = _span.byteLength();
    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), this->objectSize());

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    // d_assert_msg(objectSize == static_cast<size_t>(objectSize()), "%zu != %hu", objectSize, _objectSize);

    d_assert(_nextMiniHeap == 0);

    // dumpDebug();
  }

  ~MiniHeap() {
    // if (_meshCount > 1)
    //   dumpDebug();
  }

  inline Span span() const {
    return _span;
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"mesh-count\": %d, \"bitmap\": \"%s\"}\n",
                this, objectSize(), maxCount(), meshCount(), _bitmap.to_string().c_str());
  }

  // should never be called directly, a freelist is populated from our bitmap
  // inline void *malloc();
  // inline void localFree(Freelist &freelist, MWC &prng, void *ptr);

  inline void free(void *arenaBegin, void *ptr) {
    const ssize_t off = getOff(arenaBegin, ptr);
    if (unlikely(off < 0))
      return;

    freeOff(off);
  }

protected:
  inline void freeOff(size_t off) {
    d_assert(_bitmap.isSet(off));
    _bitmap.unset(off);
  }

public:
  /// Copies (for meshing) the contents of src into our span.
  inline void consume(void *arenaBegin, MiniHeap *src) {
    // this would be bad
    d_assert(src != this);
    d_assert(objectSize() == src->objectSize());

    src->setMeshed();
    const auto srcSpan = src->getSpanStart(arenaBegin);

    // for each object in src, copy it to our backing span + update
    // our bitmap and in-use count
    for (auto const &off : src->bitmap()) {
      d_assert(!_bitmap.isSet(off));
      void *srcObject = reinterpret_cast<void *>(srcSpan + off * objectSize());
      // need to ensure we update the bitmap and in-use count
      void *dstObject = mallocAt(arenaBegin, off);
      // debug("meshing: %zu (%p <- %p, %zu)\n", off, dstObject, srcObject, objectSize());
      d_assert(dstObject != nullptr);
      memcpy(dstObject, srcObject, objectSize());
      // debug("\t'%s'\n", dstObject);
      // debug("\t'%s'\n", srcObject);
      src->freeOff(off);
    }

    trackMeshedSpan(GetMiniHeapID(src));
  }

  inline size_t spanSize() const {
    return _span.byteLength();
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  inline size_t objectSize() const {
    return 1.0 / _objectSizeReciprocal;
  }

  inline size_t getSize() const {
    return objectSize();
  }

  inline uintptr_t getSpanStart(void *arenaBegin) const {
    const auto beginval = reinterpret_cast<uintptr_t>(arenaBegin);
    return beginval + _span.offset * kPageSize;
  }

  inline bool isEmpty() const {
    return _bitmap.inUseCount() == 0;
  }

  inline bool isFull() const {
    return _bitmap.inUseCount() == maxCount();
  }

  inline size_t inUseCount() const {
    return _bitmap.inUseCount();
  }

  inline void setMeshed() {
    _flags.setMeshed();
  }

  inline void setAttached() {
    _flags.setAttached();
  }

  inline void unsetAttached() {
    _flags.unsetAttached();
  }

  inline bool isAttached() const {
    return _flags.isAttached();
  }

  inline bool isMeshed() const {
    return _flags.isMeshed();
  }

  inline bool isMeshingCandidate() const {
    return !_flags.isAttached() && objectSize() < kPageSize;
  }

  /// Returns the fraction full (in the range [0, 1]) that this miniheap is.
  inline double fullness() const {
    return static_cast<double>(inUseCount()) / static_cast<double>(maxCount());
  }

  const internal::Bitmap &bitmap() const {
    return _bitmap;
  }

  internal::Bitmap &writableBitmap() {
    return _bitmap;
  }

  void trackMeshedSpan(MiniHeapID id) {
    if (id == 0) {
      return;
    }

    if (unlikely(meshCount() >= kMaxMeshes)) {
      mesh::debug("fatal: too many meshes for one miniheap");
      dumpDebug();
      abort();
    }

    if (_nextMiniHeap == 0) {
      _nextMiniHeap = id;
    } else {
      GetMiniHeap(_nextMiniHeap)->trackMeshedSpan(id);
    }
  }

public:
  template <class Callback>
  inline void forEachMeshed(Callback cb) const {
    if (cb(this))
      return;

    if (_nextMiniHeap != 0) {
      const auto mh = GetMiniHeap(_nextMiniHeap);
      mh->forEachMeshed(cb);
    }
  }

  template <class Callback>
  inline void forEachMeshed(Callback cb) {
    if (cb(this))
      return;

    if (_nextMiniHeap != 0) {
      auto mh = GetMiniHeap(_nextMiniHeap);
      mh->forEachMeshed(cb);
    }
  }

  size_t meshCount() const {
    size_t count = 0;
    forEachMeshed([&](const MiniHeap *mh) {
      count++;
      return false;
    });
    return count;
  }

  inline void forEachSpan(SpanCallback cb) const {
  }

  internal::BinToken getBinToken() const {
    return _token;
  }

  void setBinToken(internal::BinToken token) {
    _token = token;
  }

  /// public for meshTest only
  inline void *mallocAt(void *arenaBegin, size_t off) {
    if (!_bitmap.tryToSet(off)) {
      mesh::debug("%p: MA %u", this, off);
      dumpDebug();
      return nullptr;
    }

    return ptrFromOffset(arenaBegin, off);
  }

  inline void *ptrFromOffset(void *arenaBegin, size_t off) {
    return reinterpret_cast<void *>(getSpanStart(arenaBegin) + off * objectSize());
  }

  inline bool operator<(MiniHeap *&rhs) noexcept {
    return this->inUseCount() < rhs->inUseCount();
  }

  void dumpDebug() const {
    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = this->inUseCount();
    const size_t meshCount = this->meshCount();
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (inUse: %zu, mesh: %zu)\t%p-%p\n", this, objectSize(),
                maxCount(), heapPages, inUseCount, meshCount, _span.offset * kPageSize,
                _span.offset * kPageSize + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string().c_str());
  }

  inline int bitmapGet(void *arenaBegin, enum mesh::BitType type, void *ptr) const {
    const ssize_t off = getOff(arenaBegin, ptr);
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

  inline int bitmapSet(void *arenaBegin, enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(arenaBegin, ptr);
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

  inline int bitmapClear(void *arenaBegin, enum mesh::BitType type, void *ptr) {
    const ssize_t off = getOff(arenaBegin, ptr);
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

  inline ssize_t getOff(void *arenaBegin, void *ptr) const {
    const auto span = spanStart(arenaBegin, ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    // const size_t off = (ptrval - span) / _objectSize;
    const size_t off = (ptrval - span) * _objectSizeReciprocal;
    // hard_assert_msg(off == off2, "%zu != %zu", off, off2);

    return off;
  }

protected:
  inline uintptr_t spanStart(void *arenaBegin, void *ptr) const {
    const auto beginval = reinterpret_cast<uintptr_t>(arenaBegin);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto len = _span.byteLength();

    // manually unroll loop once to capture the common case of
    // un-meshed miniheaps
    uintptr_t span = beginval + _span.offset * kPageSize;
    if (likely(span <= ptrval && ptrval < span + len))
      return span;

    span = 0;
    // TODO: investigate un-unrolling the above
    forEachMeshed([&](const MiniHeap *mh) {
      if (mh == this)
        return false;

      uintptr_t meshedSpan = beginval + mh->span().offset * kPageSize;
      if (meshedSpan <= ptrval && ptrval < meshedSpan + len) {
        span = meshedSpan;
        return true;
      }
      return false;
    });

    return span;
  }

  internal::Bitmap _bitmap;  // 32 bytes 32
  internal::BinToken _token{
      internal::BinToken::Max,
      internal::BinToken::Max,
  };                                  // 8        40
  const Span _span;                   // 8        48
  Flags _flags{};                     // 4        52
  const uint32_t _maxCount;           // 4        56
  MiniHeapID _nextMiniHeap{0};        // 4        60
  const float _objectSizeReciprocal;  // 4        64

#ifdef MESH_EXTRA_BITS
  internal::Bitmap _bitmap0;
  internal::Bitmap _bitmap1;
  internal::Bitmap _bitmap2;
  internal::Bitmap _bitmap3;
#endif
};

static_assert(sizeof(mesh::internal::Bitmap) == 32, "Bitmap too big!");
#ifdef MESH_EXTRA_BITS
// static_assert(sizeof(MiniHeap) == 184, "MiniHeap too big!");
#else
static_assert(sizeof(MiniHeap) == 64, "MiniHeap too big!");
#endif
// static_assert(sizeof(MiniHeap) == 80, "MiniHeap too big!");
}  // namespace mesh

#endif  // MESH__MINIHEAP_H
