// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_MINI_HEAP_H
#define MESH_MINI_HEAP_H

#include <pthread.h>

#include <atomic>
#include <random>

#include "bitmap.h"
#include "fixed_array.h"
#include "internal.h"
#include "size_class_reciprocals.h"

#include "rng/mwc.h"

#include "heaplayers.h"

namespace mesh {

class Flags {
private:
  DISALLOW_COPY_AND_ASSIGN(Flags);

  static inline constexpr uint32_t ATTRIBUTE_ALWAYS_INLINE getSingleBitMask(uint32_t pos) {
    return 1UL << pos;
  }
  static constexpr uint32_t SizeClassShift = 0;
  static constexpr uint32_t FreelistIdShift = 6;
  static constexpr uint32_t ShuffleVectorOffsetShift = 8;
  static constexpr uint32_t MaxCountShift = 16;
  static constexpr uint32_t PendingOffset = 27;
  static constexpr uint32_t MeshedOffset = 30;

  inline void ATTRIBUTE_ALWAYS_INLINE setMasked(uint32_t mask, uint32_t newVal) {
    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_flags,
                                                  &oldFlags,                   // old val
                                                  (oldFlags & mask) | newVal,  // new val
                                                  std::memory_order_release,   // success mem model
                                                  std::memory_order_relaxed)) {
    }
  }

public:
  explicit Flags(uint32_t maxCount, uint32_t sizeClass, uint32_t svOffset, uint32_t freelistId) noexcept
      : _flags{(maxCount << MaxCountShift) + (sizeClass << SizeClassShift) + (svOffset << ShuffleVectorOffsetShift) +
               (freelistId << FreelistIdShift)} {
    d_assert((freelistId & 0x3) == freelistId);
    d_assert((sizeClass & ((1 << FreelistIdShift) - 1)) == sizeClass);
    d_assert(svOffset < 255);
    d_assert_msg(sizeClass < 255, "sizeClass: %u", sizeClass);
    d_assert_msg(maxCount <= 1024, "maxCount: %u (max 1024 for bitmap limit)", maxCount);
    d_assert(this->maxCount() == maxCount);
  }

  inline uint32_t freelistId() const {
    return (_flags.load(std::memory_order_acquire) >> FreelistIdShift) & 0x3;
  }

  inline void setFreelistId(uint32_t freelistId) {
    static_assert(list::Max <= (1 << FreelistIdShift), "expected max < 4");
    d_assert(freelistId < list::Max);
    uint32_t mask = ~(static_cast<uint32_t>(0x3) << FreelistIdShift);
    uint32_t newVal = (static_cast<uint32_t>(freelistId) << FreelistIdShift);
    setMasked(mask, newVal);
  }

  // Atomically set pending flag if current state is Full.
  // FreelisId remains Full. Returns true on success.
  inline bool trySetPendingFromFull() {
    constexpr uint32_t freelistMask = static_cast<uint32_t>(0x3) << FreelistIdShift;
    constexpr uint32_t fullVal = static_cast<uint32_t>(list::Full) << FreelistIdShift;
    constexpr uint32_t pendingBit = static_cast<uint32_t>(1) << PendingOffset;

    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (true) {
      if ((oldFlags & freelistMask) != fullVal) {
        return false;
      }
      if (oldFlags & pendingBit) {
        return false;  // Already pending
      }
      // Set pending flag, keep freelistId as Full
      uint32_t desired = oldFlags | pendingBit;
      if (_flags.compare_exchange_weak(oldFlags, desired, std::memory_order_release, std::memory_order_relaxed)) {
        return true;
      }
    }
  }

  inline uint32_t maxCount() const {
    // XXX: does this assume little endian?
    // 0x7ff = 11 bits, supports values up to 2047 (we cap at 1024 for bitmap limit)
    return (_flags.load(std::memory_order_acquire) >> MaxCountShift) & 0x7ff;
  }

  inline uint32_t sizeClass() const {
    return (_flags.load(std::memory_order_acquire) >> SizeClassShift) & 0x3f;
  }

  inline uint8_t svOffset() const {
    return (_flags.load(std::memory_order_acquire) >> ShuffleVectorOffsetShift) & 0xff;
  }

  inline void setSvOffset(uint8_t off) {
    d_assert(off < 255);
    uint32_t mask = ~(static_cast<uint32_t>(0xff) << ShuffleVectorOffsetShift);
    uint32_t newVal = (static_cast<uint32_t>(off) << ShuffleVectorOffsetShift);
    setMasked(mask, newVal);
  }

  inline void setMeshed() {
    set(MeshedOffset);
  }

  inline void unsetMeshed() {
    unset(MeshedOffset);
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isMeshed() const {
    return is(MeshedOffset);
  }

  inline void setPending() {
    set(PendingOffset);
  }

  inline void clearPending() {
    unset(PendingOffset);
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isPending() const {
    return is(PendingOffset);
  }

private:
  inline bool ATTRIBUTE_ALWAYS_INLINE is(size_t offset) const {
    const auto mask = getSingleBitMask(offset);
    return (_flags.load(std::memory_order_acquire) & mask) == mask;
  }

  inline void set(size_t offset) {
    const uint32_t mask = getSingleBitMask(offset);

    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_flags,
                                                  &oldFlags,                  // old val
                                                  oldFlags | mask,            // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }
  }

  inline void unset(size_t offset) {
    const uint32_t mask = getSingleBitMask(offset);

    uint32_t oldFlags = _flags.load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_flags,
                                                  &oldFlags,                  // old val
                                                  oldFlags & ~mask,           // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }
  }

  std::atomic<uint32_t> _flags;
};

template <size_t PageSize>
class MiniHeap {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeap);

public:
  using BitmapType = internal::Bitmap<PageSize>;
  using ListEntryType = MiniHeapListEntry<PageSize>;
  static constexpr size_t kPageSize = PageSize;
  static constexpr unsigned kPageShift = __builtin_ctzl(PageSize);

  MiniHeap(void *arenaBegin, Span span, size_t objectCount, size_t objectSize)
      : _span(span),
        _flags(objectCount, objectCount > 1 ? SizeMap::SizeClass(objectSize) : 1, 0, list::Attached),
        _bitmap(objectCount) {
    d_assert(_bitmap.inUseCount() == 0);

    const auto expectedSpanSize = static_cast<size_t>(_span.length) << kPageShift;
    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), this->objectSize());

    d_assert(!_nextMeshed.hasValue());
  }

  inline Span span() const {
    return _span;
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"mesh-count\": %d, \"bitmap\": \"%s\"}\n",
                this, objectSize(), maxCount(), meshCount(), _bitmap.to_string(maxCount()).c_str());
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(void *arenaBegin, void *ptr) {
    // the logic in globalFree is
    // updated to allow the 'race' between lock-free freeing and
    // meshing
    // d_assert(!isMeshed());
    const ssize_t off = getOff(arenaBegin, ptr);
    if (unlikely(off < 0)) {
      d_assert(false);
      return;
    }

    freeOff(off);
  }

  inline bool clearIfNotFree(void *arenaBegin, void *ptr) {
    const ssize_t off = getOff(arenaBegin, ptr);
    const auto notWasSet = _bitmap.unset(off);
    const auto wasSet = !notWasSet;
    return wasSet;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE freeOff(size_t off) {
    d_assert_msg(_bitmap.isSet(off), "MiniHeap(%p) expected bit %zu to be set (svOff:%zu)", this, off, svOffset());
    _bitmap.unset(off);
  }

  /// Copies (for meshing) the contents of src into our span.
  inline void consume(const void *arenaBegin, MiniHeap *src) {
    // this would be bad
    d_assert(src != this);
    d_assert(objectSize() == src->objectSize());

    src->setMeshed();
    const auto srcSpan = src->getSpanStart(arenaBegin);
    const auto objectSize = this->objectSize();

    // this both avoids the need to call `freeOff` in the loop
    // below, but it ensures we will be able to check for bitmap
    // setting races in GlobalHeap::freeFor
    const auto srcBitmap = src->takeBitmap();

    // for each object in src, copy it to our backing span + update
    // our bitmap and in-use count
    for (auto const &off : srcBitmap) {
      d_assert(off < maxCount());
      d_assert(!_bitmap.isSet(off));

      void *srcObject = reinterpret_cast<void *>(srcSpan + off * objectSize);
      // need to ensure we update the bitmap and in-use count
      void *dstObject = mallocAt(arenaBegin, off);
      // debug("meshing: %zu (%p <- %p)\n", off, dstObject, srcObject);
      d_assert(dstObject != nullptr);
      memcpy(dstObject, srcObject, objectSize);
      // debug("\t'%s'\n", dstObject);
      // debug("\t'%s'\n", srcObject);
    }

    trackMeshedSpan(GetMiniHeapID(src));
  }

  inline size_t spanSize() const {
    return static_cast<size_t>(_span.length) << kPageShift;
  }

  inline uint32_t ATTRIBUTE_ALWAYS_INLINE maxCount() const {
    return _flags.maxCount();
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isLargeAlloc() const {
    return maxCount() == 1;
  }

  inline size_t objectSize() const {
    if (likely(!isLargeAlloc())) {
      return static_cast<size_t>(SizeMap::class_to_size(sizeClass()));
    } else {
      return static_cast<size_t>(_span.length) << kPageShift;
    }
  }

  inline int sizeClass() const {
    return _flags.sizeClass();
  }

  inline uintptr_t getSpanStart(const void *arenaBegin) const {
    const auto beginval = reinterpret_cast<uintptr_t>(arenaBegin);
    return beginval + (static_cast<size_t>(_span.offset) << kPageShift);
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isEmpty() const {
    return _bitmap.inUseCount() == 0;
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isFull() const {
    return _bitmap.inUseCount() == maxCount();
  }

  inline uint32_t ATTRIBUTE_ALWAYS_INLINE inUseCount() const {
    return _bitmap.inUseCount();
  }

  inline size_t bytesFree() const {
    return (maxCount() - inUseCount()) * objectSize();
  }

  inline void setMeshed() {
    _flags.setMeshed();
  }

  inline void setAttached(pid_t current, ListEntryType *listHead) {
    // mesh::debug("MiniHeap(%p:%5zu): current <- %u\n", this, objectSize(), current);
    _current.store(current, std::memory_order::memory_order_release);
    if (listHead != nullptr) {
      _freelist.remove(listHead);
    }
    this->setFreelistId(list::Attached);
  }

  inline uint8_t svOffset() const {
    return _flags.svOffset();
  }

  inline void setSvOffset(uint8_t off) {
    // debug("MiniHeap(%p) SET svOff:%zu)", this, off);
    _flags.setSvOffset(off);
  }

  inline uint8_t freelistId() const {
    return _flags.freelistId();
  }

  inline void setFreelistId(uint8_t id) {
    _flags.setFreelistId(id);
  }

  // Atomically set pending flag if current state is Full.
  inline bool trySetPendingFromFull() {
    return _flags.trySetPendingFromFull();
  }

  inline bool isPending() const {
    return _flags.isPending();
  }

  inline void clearPending() {
    _flags.clearPending();
  }

  inline MiniHeapID pendingNext() const {
    return _pendingNext;
  }

  inline void setPendingNext(MiniHeapID next) {
    _pendingNext = next;
  }

  inline pid_t current() const {
    return _current.load(std::memory_order::memory_order_acquire);
  }

  inline void unsetAttached() {
    // mesh::debug("MiniHeap(%p:%5zu): current <- UNSET\n", this, objectSize());
    _current.store(0, std::memory_order::memory_order_release);
  }

  inline bool isAttached() const {
    return current() != 0;
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE isMeshed() const {
    return _flags.isMeshed();
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE hasMeshed() const {
    return _nextMeshed.hasValue();
  }

  inline bool isMeshingCandidate() const {
    return !isAttached() && objectSize() < PageSize;
  }

  /// Returns the fraction full (in the range [0, 1]) that this miniheap is.
  inline double fullness() const {
    return static_cast<double>(inUseCount()) / static_cast<double>(maxCount());
  }

  template <size_t MaxBits = PageSize / kMinObjectSize>
  internal::RelaxedFixedBitmap<PageSize> takeBitmap() {
    const auto capacity = this->maxCount();
    internal::RelaxedFixedBitmap<PageSize> zero{capacity};
    internal::RelaxedFixedBitmap<PageSize> result{capacity};
    _bitmap.setAndExchangeAll(result.mut_bits(), zero.bits());
    return result;
  }

  const BitmapType &bitmap() const {
    return _bitmap;
  }

  BitmapType &writableBitmap() {
    return _bitmap;
  }

  void trackMeshedSpan(MiniHeapID id) {
    hard_assert(id.hasValue());

    if (!_nextMeshed.hasValue()) {
      _nextMeshed = id;
    } else {
      GetMiniHeap<MiniHeap>(_nextMeshed)->trackMeshedSpan(id);
    }
  }

public:
  template <class Callback>
  inline void forEachMeshed(Callback cb) const {
    if (cb(this))
      return;

    if (_nextMeshed.hasValue()) {
      const auto mh = GetMiniHeap<MiniHeap>(_nextMeshed);
      mh->forEachMeshed(cb);
    }
  }

  template <class Callback>
  inline void forEachMeshed(Callback cb) {
    if (cb(this))
      return;

    if (_nextMeshed.hasValue()) {
      auto mh = GetMiniHeap<MiniHeap>(_nextMeshed);
      mh->forEachMeshed(cb);
    }
  }

  bool isRelated(MiniHeap *other) const {
    auto otherFound = false;
    this->forEachMeshed([&](const MiniHeap *eachMh) {
      const auto found = eachMh == other;
      otherFound = found;
      return found;
    });
    return otherFound;
  }

  size_t meshCount() const {
    size_t count = 0;

    const MiniHeap *mh = this;
    while (mh != nullptr) {
      count++;

      auto next = mh->_nextMeshed;
      mh = next.hasValue() ? GetMiniHeap<MiniHeap>(next) : nullptr;
    }

    return count;
  }

  ListEntryType *getFreelist() {
    return &_freelist;
  }

  /// public for meshTest only
  inline void *mallocAt(const void *arenaBegin, size_t off) {
    if (!_bitmap.tryToSet(off)) {
      mesh::debug("%p: MA %u", this, off);
      dumpDebug();
      return nullptr;
    }

    return ptrFromOffset(arenaBegin, off);
  }

  inline void *ptrFromOffset(const void *arenaBegin, size_t off) {
    return reinterpret_cast<void *>(getSpanStart(arenaBegin) + off * objectSize());
  }

  inline bool operator<(MiniHeap *&rhs) noexcept {
    return this->inUseCount() < rhs->inUseCount();
  }

  void dumpDebug() const {
    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = this->inUseCount();
    const size_t meshCount = this->meshCount();
    const auto spanOffset = static_cast<size_t>(_span.offset) << kPageShift;
    mesh::debug(
        "MiniHeap(%p:%5zu): %3zu objects on %2zu pages (inUse: %zu, spans: %zu)\t%p-%p\tFreelist{prev:%u, next:%u}\n",
        this, objectSize(), maxCount(), heapPages, inUseCount, meshCount, spanOffset, spanOffset + spanSize(),
        _freelist.prev(), _freelist.next());
    mesh::debug("\t%s\n", _bitmap.to_string(maxCount()).c_str());
  }

  // this only works for unmeshed miniheaps
  inline uint16_t ATTRIBUTE_ALWAYS_INLINE getUnmeshedOff(const void *arenaBegin, void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    uintptr_t span = reinterpret_cast<uintptr_t>(arenaBegin) + (static_cast<size_t>(_span.offset) << kPageShift);
    d_assert(span != 0);

    const size_t off = float_recip::computeIndex(ptrval - span, sizeClass());
    d_assert(off < maxCount());

    return off;
  }

  inline uint16_t ATTRIBUTE_ALWAYS_INLINE getOff(const void *arenaBegin, void *ptr) const {
    const auto span = spanStart(reinterpret_cast<uintptr_t>(arenaBegin), ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    const size_t off = float_recip::computeIndex(ptrval - span, sizeClass());
    d_assert(off < maxCount());

    return off;
  }

protected:
  inline uintptr_t ATTRIBUTE_ALWAYS_INLINE spanStart(uintptr_t arenaBegin, void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    const auto len = static_cast<size_t>(_span.length) << kPageShift;

    // manually unroll loop once to capture the common case of
    // un-meshed miniheaps
    uintptr_t spanptr = arenaBegin + (static_cast<size_t>(_span.offset) << kPageShift);
    if (likely(spanptr <= ptrval && ptrval < spanptr + len)) {
      return spanptr;
    }

    return spanStartSlowpath(arenaBegin, ptrval);
  }

  uintptr_t ATTRIBUTE_NEVER_INLINE spanStartSlowpath(uintptr_t arenaBegin, uintptr_t ptrval) const {
    const auto len = static_cast<size_t>(_span.length) << kPageShift;
    uintptr_t spanptr = 0;

    const MiniHeap *mh = this;
    while (true) {
      if (unlikely(!mh->_nextMeshed.hasValue())) {
        abort();
      }

      mh = GetMiniHeap<MiniHeap>(mh->_nextMeshed);

      const uintptr_t meshedSpanptr = arenaBegin + (static_cast<size_t>(mh->span().offset) << kPageShift);
      if (meshedSpanptr <= ptrval && ptrval < meshedSpanptr + len) {
        spanptr = meshedSpanptr;
        break;
      }
    };

    return spanptr;
  }

  const Span _span;           // 8 bytes
  ListEntryType _freelist{};  // 8 bytes
  atomic<pid_t> _current{0};  // 4 bytes
  Flags _flags;               // 4 bytes
  MiniHeapID _nextMeshed{};   // 4 bytes
  MiniHeapID _pendingNext{};  // 4 bytes (for lock-free pending list, separate from _freelist)
  BitmapType _bitmap;         // 32 bytes (4K) or 128 bytes (16K)
};

template <size_t PageSize>
using MiniHeapArray = FixedArray<MiniHeap<PageSize>, 63>;

static_assert(sizeof(pid_t) == 4, "pid_t not 32-bits!");
static_assert(sizeof(MiniHeap<4096>) == 64, "MiniHeap<4K> should be 64 bytes!");
static_assert(sizeof(MiniHeap<16384>) == 160, "MiniHeap<16K> should be 160 bytes!");
static_assert(sizeof(MiniHeapArray<4096>) == 64 * sizeof(void *), "MiniHeapArray too big!");
}  // namespace mesh

#endif  // MESH_MINI_HEAP_H