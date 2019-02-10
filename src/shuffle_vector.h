// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__SHUFFLE_VECTOR_H
#define MESH__SHUFFLE_VECTOR_H

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
  size_t init(internal::Bitmap &bitmap) {
    d_assert(_maxCount > 0);
    d_assert_msg(_maxCount <= kMaxShuffleVectorLength, "objCount? %zu <= %zu", _maxCount, kMaxShuffleVectorLength);

    // off == maxCount means 'empty'
    _off = _maxCount;
    // d_assert(_maxCount == _attachedMiniheap->maxCount());

    internal::RelaxedFixedBitmap newBitmap{_maxCount};
    newBitmap.setAll(_maxCount);

    internal::RelaxedFixedBitmap localBits{_maxCount};
    bitmap.setAndExchangeAll(localBits.mut_bits(), newBitmap.bits());
    localBits.invert();

    for (auto const &i : localBits) {
      // for (size_t i = 0; i < _maxCount; i++) {
      if (i >= _maxCount) {
        break;
      }
      _off--;
      d_assert(_off < _maxCount);
      _list[_off] = sv::Entry{0, static_cast<uint8_t>(i)};
    }

    if (kEnableShuffleOnInit) {
      internal::mwcShuffle(&_list[_off], &_list[_maxCount], _prng);
    }

    return length();
  }

  FixedArray<MiniHeap, 1> &miniheaps() {
    _start = 0;
    _end = 0;
    return _attachedMiniheaps;
  }

  void refillMiniheaps() {
    while (_off < _maxCount) {
      const auto entry = pop();
      _attachedMiniheaps[entry.miniheapOffset()]->freeOff(entry.bit());
    }
  }

  inline bool isExhausted() const {
    return _off >= _maxCount;
  }

  inline size_t maxCount() const {
    return _maxCount;
  }

  // number of items in the list
  inline size_t ATTRIBUTE_ALWAYS_INLINE length() const {
    return _maxCount - _off;
  }

  // Pushing an element onto the freelist does a round of the
  // Fisher-Yates shuffle if randomization level is >= 2.
  inline void ATTRIBUTE_ALWAYS_INLINE push(uint8_t freedOff) {
    d_assert(_off > 0);  // we must have at least 1 free space in the list

    _off--;
    _list[_off] = sv::Entry{0, freedOff};

    if (kEnableShuffleOnFree) {
      size_t swapOff = _prng.inRange(_off, maxCount() - 1);
      _lastOff = swapOff;
      std::swap(_list[_off], _list[swapOff]);
    }
  }

  inline sv::Entry ATTRIBUTE_ALWAYS_INLINE pop() {
    d_assert(_off < _maxCount);

    auto val = _list[_off];
    _off++;

    return val;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(MiniHeap *mh, void *ptr) {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    // const size_t off = (ptrval - _start) / _objectSize;
    const size_t off = (ptrval - _start) * _objectSizeReciprocal;
    const size_t off2 = mh->getOff(reinterpret_cast<void *>(_arenaBegin), ptr);
    hard_assert_msg(off == off2, "%zu != %zu", off, off2);

    d_assert(off < 256);

    push(static_cast<uint8_t>(off));
  }

  // an attach takes ownership of the reference to mh
  inline void attach(void *arenaBegin) {
    _arenaBegin = reinterpret_cast<uintptr_t>(arenaBegin);
    for (MiniHeap *mh : _attachedMiniheaps) {
      d_assert(mh->isAttached());

      _start = mh->getSpanStart(arenaBegin);
      _end = _start + mh->spanSize();

      const auto allocCount = init(mh->writableBitmap());
#ifndef NDEBUG
      if (allocCount == 0) {
        mh->dumpDebug();
      }
#endif
      d_assert_msg(allocCount > 0, "no free bits in MH %p", mh->getSpanStart(arenaBegin));
    }
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE contains(void *ptr) const {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    return ptrval >= _start && ptrval < _end;
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE ptrFromOffset(sv::Entry off) const {
    d_assert(off.miniheapOffset() == 0);
    return reinterpret_cast<void *>(_start + off.bit() * _objectSize);
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
  inline void setObjectSize(size_t sz) {
    _objectSize = sz;
    _objectSizeReciprocal = 1.0 / (float)sz;
    _maxCount = max(HL::CPUInfo::PageSize / sz, kMinStringLen);
    // initially, we are unattached and therefor have no capacity.
    // Setting _off to _maxCount causes isExhausted() to return true
    // so that we don't separately have to check !isAttached() in the
    // malloc fastpath.
    _off = _maxCount;
  }

private:
  uint32_t _objectSize{0};                       // 4   4
  float _objectSizeReciprocal{0.0};              // 4   8
  uintptr_t _start{0};                           // 8   16
  uintptr_t _end{0};                             // 8   24
  uintptr_t _arenaBegin;                         //
  uint16_t _maxCount{0};                         // 2   62
  uint16_t _off{0};                              // 2   64
  FixedArray<MiniHeap, 1> _attachedMiniheaps{};  // 16  80
  MWC _prng;                                     // 36  60
  volatile uint8_t _lastOff{0};                  // 1   81
  // uint8_t __padding[47];                                       // 47  128
  sv::Entry _list[kMaxShuffleVectorLength] CACHELINE_ALIGNED;  // 512 640
};

static_assert(HL::gcd<sizeof(ShuffleVector), CACHELINE_SIZE>::value == CACHELINE_SIZE,
              "ShuffleVector not multiple of cacheline size!");
static_assert(sizeof(ShuffleVector) == 640, "ShuffleVector not expected size!");
}  // namespace mesh

#endif  // MESH__SHUFFLE_VECTOR_H
