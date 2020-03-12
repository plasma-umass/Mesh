// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_SHUFFLE_VECTOR_H
#define MESH_SHUFFLE_VECTOR_H

#include <iterator>
#include <random>
#include <utility>

#include "rng/mwc.h"

#include "internal.h"

#include "mini_heap.h"

using mesh::debug;

namespace mesh {

namespace sv {
class Entry {
public:
  Entry() noexcept : _mhOffset{0}, _bitOffset{0} {
  }

  explicit Entry(uint8_t mhOff, uint8_t bitOff) : _mhOffset{mhOff}, _bitOffset{bitOff} {
  }

  Entry(const Entry &rhs) = default;

  constexpr Entry(Entry &&rhs) = default;

  Entry &operator=(const Entry &rhs) = default;

  bool operator==(const Entry &rhs) const {
    return _mhOffset == rhs._mhOffset && _bitOffset == rhs._bitOffset;
  }

  inline uint8_t ATTRIBUTE_ALWAYS_INLINE miniheapOffset() const {
    return _mhOffset;
  }

  inline uint8_t ATTRIBUTE_ALWAYS_INLINE bit() const {
    return _bitOffset;
  }

private:
  uint8_t _mhOffset;
  uint8_t _bitOffset;
};
static_assert(sizeof(Entry) == 2, "Entry too big!");
}  // namespace sv

class ShuffleVector {
private:
  DISALLOW_COPY_AND_ASSIGN(ShuffleVector);

public:
  ShuffleVector() : _prng(internal::seed(), internal::seed()) {
    // set initialized = false;
  }

  ~ShuffleVector() {
    d_assert(_attachedMiniheaps.size() == 0);
  }

  // post: list has the index of all bits set to 1 in it, in a random order
  inline uint32_t ATTRIBUTE_ALWAYS_INLINE refillFrom(uint8_t mhOffset, internal::Bitmap &bitmap) {
    d_assert(_maxCount > 0);
    d_assert_msg(_maxCount <= kMaxShuffleVectorLength, "objCount? %zu <= %zu", _maxCount, kMaxShuffleVectorLength);

    if (isFull()) {
      return 0;
    }

    // d_assert(_maxCount == _attachedMiniheap->maxCount());

    internal::RelaxedFixedBitmap newBitmap{static_cast<uint32_t>(_maxCount)};
    newBitmap.setAll(_maxCount);

    internal::RelaxedFixedBitmap localBits{static_cast<uint32_t>(_maxCount)};
    bitmap.setAndExchangeAll(localBits.mut_bits(), newBitmap.bits());
    localBits.invert();

    uint32_t allocCount = 0;

    const uint32_t maxCount = static_cast<uint32_t>(_maxCount);
    for (auto const &i : localBits) {
      // FIXME: this incredibly lurky conditional is because
      // RelaxedFixedBitmap iterates over all 256 bits it has,
      // regardless of the _maxCount set in the constructor -- we
      // should fix that.
      if (i >= maxCount) {
        break;
      }

      if (unlikely(isFull())) {
        // TODO: we don't have any more space in our shuffle vector
        // for these bits we've pulled out of the MiniHeap's bitmap,
        // so we need to set them as free again.  we should measure
        // how often this happens, as its gonna be slow
        refillFullSlowpath(bitmap, i);
      } else {
        _off--;
        d_assert(_off >= 0);
        d_assert(_off < _maxCount);
        _list[_off] = sv::Entry{mhOffset, static_cast<uint8_t>(i)};
        allocCount++;
      }
    }

    return allocCount;
  }

  void ATTRIBUTE_NEVER_INLINE refillFullSlowpath(internal::Bitmap &bitmap, size_t i) {
    bitmap.unset(i);
  }

  FixedArray<MiniHeap, kMaxMiniheapsPerShuffleVector> &miniheaps() {
    return _attachedMiniheaps;
  }

  void refillMiniheaps() {
    while (_off < _maxCount) {
      const auto entry = pop();
      _attachedMiniheaps[entry.miniheapOffset()]->freeOff(entry.bit());
    }
  }

  inline bool isFull() const {
    return _off <= 0;
  }

  inline bool isExhausted() const {
    return _off >= _maxCount;
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE localRefill() {
    uint32_t addedCapacity = 0;
    const auto miniheapCount = _attachedMiniheaps.size();
    for (uint32_t i = 0; i < miniheapCount && !isFull(); i++, _attachedOff++) {
      if (_attachedOff >= miniheapCount) {
        _attachedOff = 0;
      }

      auto mh = _attachedMiniheaps[_attachedOff];
      if (mh->isFull()) {
        continue;
      }

      const auto allocCount = refillFrom(_attachedOff, mh->writableBitmap());
      addedCapacity |= allocCount;
    }

    if (addedCapacity > 0) {
      if (kEnableShuffleOnInit) {
        internal::mwcShuffle(&_list[_off], &_list[_maxCount], _prng);
      }
      return true;
    }

    return false;
  }

  // number of items in the list
  inline uint32_t ATTRIBUTE_ALWAYS_INLINE length() const {
    return _maxCount - _off;
  }

  // Pushing an element onto the freelist does a round of the
  // Fisher-Yates shuffle if randomization level is >= 2.
  inline void ATTRIBUTE_ALWAYS_INLINE push(sv::Entry entry) {
    d_assert(_off > 0);  // we must have at least 1 free space in the list

    _off--;
    _list[_off] = entry;

    if (kEnableShuffleOnFree) {
      size_t swapOff = _prng.inRange(_off, maxCount() - 1);
      std::swap(_list[_off], _list[swapOff]);
    }
  }

  inline sv::Entry ATTRIBUTE_ALWAYS_INLINE pop() {
    d_assert(_off >= 0);
    d_assert(_off < _maxCount);

    auto val = _list[_off];
    _off++;

    return val;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(MiniHeap *mh, void *ptr) {
    // const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    // const size_t off = (ptrval - _start) / _objectSize;
    // const size_t off = (ptrval - _start) * _objectSizeReciprocal;
    const size_t off = mh->getUnmeshedOff(reinterpret_cast<const void *>(_arenaBegin), ptr);
    // hard_assert_msg(off == off2, "%zu != %zu", off, off2);

    d_assert(off < 256);

    if (likely(_off > 0)) {
      push(sv::Entry{mh->svOffset(), static_cast<uint8_t>(off)});
    } else {
      freeFullSlowpath(mh, off);
    }
  }

  void ATTRIBUTE_NEVER_INLINE freeFullSlowpath(MiniHeap *mh, size_t off) {
    mh->freeOff(off);
  }

  // an attach takes ownership of the reference to mh
  inline void reinit() {
    _off = _maxCount;
    _attachedOff = 0;

    internal::mwcShuffle(_attachedMiniheaps.array_begin(), _attachedMiniheaps.array_end(), _prng);

    for (size_t i = 0; i < _attachedMiniheaps.size(); i++) {
      const auto mh = _attachedMiniheaps[i];
      _start[i] = mh->getSpanStart(_arenaBegin);
      mh->setSvOffset(i);
      d_assert(mh->isAttached());
    }

    const bool addedCapacity = localRefill();
    d_assert(addedCapacity);
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE ptrFromOffset(sv::Entry off) const {
    d_assert(off.miniheapOffset() < _attachedMiniheaps.size());
    return reinterpret_cast<void *>(_start[off.miniheapOffset()] + off.bit() * _objectSize);
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE malloc() {
    d_assert(!isExhausted());
    const auto off = pop();
    return ptrFromOffset(off);
  }

  inline size_t getSize() {
    return _objectSize;
  }

  // called once, on initialization of ThreadLocalHeap
  inline void initialInit(const char *arenaBegin, uint32_t sz) {
    _arenaBegin = arenaBegin;
    _objectSize = sz;
    _objectSizeReciprocal = 1.0 / (float)sz;
    _maxCount = max(kPageSize / sz, kMinStringLen);
    // initially, we are unattached and therefor have no capacity.
    // Setting _off to _maxCount causes isExhausted() to return true
    // so that we don't separately have to check !isAttached() in the
    // malloc fastpath.
    _off = _maxCount;
  }

private:
  uintptr_t _start[kMaxMiniheapsPerShuffleVector];                           // 32  32
  const char *_arenaBegin;                                                   // 8   40
  int16_t _maxCount{0};                                                      // 2   42
  int16_t _off{0};                                                           // 2   44
  uint32_t _objectSize{0};                                                   // 4   48
  FixedArray<MiniHeap, kMaxMiniheapsPerShuffleVector> _attachedMiniheaps{};  // 36  128
  MWC _prng;                                                                 // 36  84
  float _objectSizeReciprocal{0.0};                                          // 4   88
  uint32_t _attachedOff{0};                                                  //
  sv::Entry _list[kMaxShuffleVectorLength] CACHELINE_ALIGNED;                // 512 640
};

static_assert(HL::gcd<sizeof(ShuffleVector), CACHELINE_SIZE>::value == CACHELINE_SIZE,
              "ShuffleVector not multiple of cacheline size!");
// FIXME should fit in 640
// static_assert(sizeof(ShuffleVector) == 704, "ShuffleVector not expected size!");
}  // namespace mesh

#endif  // MESH_SHUFFLE_VECTOR_H
