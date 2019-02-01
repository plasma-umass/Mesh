// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINI_HEAP_H
#define MESH__MINI_HEAP_H

#include <pthread.h>

#include <atomic>
#include <random>

#include "bitmap.h"
#include "internal.h"

#include "rng/mwc.h"

#include "heaplayers.h"

namespace mesh {

class MiniHeap;

class Flags {
private:
  DISALLOW_COPY_AND_ASSIGN(Flags);

  static inline constexpr uint32_t ATTRIBUTE_ALWAYS_INLINE getMask(uint32_t pos) {
    return 1UL << pos;
  }
  static constexpr uint32_t MeshedOffset = 16;
  static constexpr uint32_t AttachedOffset = 24;

public:
  explicit Flags(uint32_t maxCount) noexcept : _flags{maxCount} {
    d_assert(maxCount <= 256);
    d_assert(this->maxCount() == maxCount);
  }

  inline uint32_t maxCount() const {
    // XXX: does this assume little endian?
    return _flags.load(std::memory_order_relaxed) & 0x1ff;
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

  atomic_uint32_t _flags;
};

class MiniHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeap);

public:
  MiniHeap(void *arenaBegin, Span span, size_t objectCount, size_t objectSize)
      : _bitmap(objectCount),
        _span(span),
        _flags(objectCount),
        _objectSize(objectSize),
        _objectSizeReciprocal(1.0 / (float)objectSize) {
    // debug("sizeof(MiniHeap): %zu", sizeof(MiniHeap));

    d_assert(_bitmap.inUseCount() == 0);

    const auto expectedSpanSize = _span.byteLength();
    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), this->objectSize());

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    // d_assert_msg(objectSize == static_cast<size_t>(objectSize()), "%zu != %hu", objectSize, _objectSize);

    d_assert(!_nextMiniHeap.hasValue());

    // debug("new:\n");
    // dumpDebug();
  }

  ~MiniHeap() {
    // debug("destruct:\n");
    // dumpDebug();
  }

  inline Span span() const {
    return _span;
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"mesh-count\": %d, \"bitmap\": \"%s\"}\n",
                this, objectSize(), maxCount(), meshCount(), _bitmap.to_string(maxCount()).c_str());
  }

  inline void free(void *arenaBegin, void *ptr) {
    // TODO: this should be removed when the logic in globalFree is
    // updated to allow the 'race' between lock-free freeing and
    // meshing
    d_assert(!isMeshed());
    const ssize_t off = getOff(arenaBegin, ptr);
    if (unlikely(off < 0)) {
      d_assert(false);
      return;
    }

    freeOff(off);
  }

  inline void freeOff(size_t off) {
    d_assert(_bitmap.isSet(off));
    _bitmap.unset(off);
  }

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
      d_assert(off < maxCount());
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
    return _flags.maxCount();
  }

  inline size_t objectSize() const {
    return _objectSize;
  }

  inline int sizeClass() const {
    return SizeMap::SizeClass(_objectSize);
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
    hard_assert(id.hasValue());

    if (unlikely(meshCount() >= kMaxMeshes)) {
      mesh::debug("fatal: too many meshes for one miniheap");
      dumpDebug();
      abort();
    }

    if (!_nextMiniHeap.hasValue()) {
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

    if (_nextMiniHeap.hasValue()) {
      const auto mh = GetMiniHeap(_nextMiniHeap);
      mh->forEachMeshed(cb);
    }
  }

  template <class Callback>
  inline void forEachMeshed(Callback cb) {
    if (cb(this))
      return;

    if (_nextMiniHeap.hasValue()) {
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
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (inUse: %zu, spans: %zu)\t%p-%p\n", this, objectSize(),
                maxCount(), heapPages, inUseCount, meshCount, _span.offset * kPageSize,
                _span.offset * kPageSize + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string(maxCount()).c_str());
  }

  inline ssize_t getOff(void *arenaBegin, void *ptr) const {
    const auto span = spanStart(arenaBegin, ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    const size_t off = (ptrval - span) * _objectSizeReciprocal;
#ifndef NDEBUG
    const size_t off2 = (ptrval - span) / _objectSize;
    hard_assert_msg(off == off2, "%zu != %zu", off, off2);
#endif

    d_assert(off < maxCount());

    return off;
  }

protected:
  inline uintptr_t spanStart(void *arenaBegin, void *ptr) const {
    const auto arena = reinterpret_cast<uintptr_t>(arenaBegin);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto len = _span.byteLength();

    // manually unroll loop once to capture the common case of
    // un-meshed miniheaps
    uintptr_t spanptr = arena + _span.offset * kPageSize;
    if (likely(spanptr <= ptrval && ptrval < spanptr + len))
      return spanptr;

    spanptr = 0;
    if (!_nextMiniHeap.hasValue()) {
      d_assert(false);
      return spanptr;
    }

    GetMiniHeap(_nextMiniHeap)->forEachMeshed([&](const MiniHeap *mh) {
      uintptr_t meshedSpanptr = arena + mh->span().offset * kPageSize;
      if (meshedSpanptr <= ptrval && ptrval < meshedSpanptr + len) {
        spanptr = meshedSpanptr;
        return true;
      }
      return false;
    });

    d_assert(spanptr != 0);
    return spanptr;
  }

  internal::Bitmap _bitmap;  // 32 bytes 32
  internal::BinToken _token{
      internal::bintoken::BinMax,
      internal::bintoken::Max,
  };                                  // 8        40
  const Span _span;                   // 8        48
  Flags _flags;                       // 4        52
  const uint32_t _objectSize;         // 4        56
  const float _objectSizeReciprocal;  // 4        60
  MiniHeapID _nextMiniHeap{};         // 4        64
};

static_assert(sizeof(pid_t) == 4, "pid_t not 32-bits!");
static_assert(sizeof(mesh::internal::Bitmap) == 32, "Bitmap too big!");
static_assert(sizeof(MiniHeap) == 64, "MiniHeap too big!");
}  // namespace mesh

#endif  // MESH__MINI_HEAP_H
