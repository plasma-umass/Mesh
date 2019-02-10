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

  FixedArray<MiniHeap, kMaxMiniheapsPerShuffleVector> &miniheaps() {
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

  inline bool ATTRIBUTE_ALWAYS_INLINE localRefill() {
    auto miniheapCount = _attachedMiniheaps.size();
    for (uint32_t i = 0, mhOff = _attachedOff + i; i < miniheapCount; i++, mhOff++) {
      if (mhOff == miniheapCount) {
        mhOff = 0;
      }
    }
    // const auto origOff = _attachedOff;
    // // look at the rest of the MiniHeaps we already have.  If some have space,
    // // refill the shuffle vector
    // for (_attachedOff++; _attachedOff < _attachedMiniheaps.size(); _attachedOff++) {
    //   MiniHeap *mh = getAttached();
    //   if (mh->fullness() > .1) {
    //     attach(arenaBegin, _attachedOff);
    //     return true;
    //   }
    // }
    // // TODO: refactor this, but if we didn't find anything we should loop
    // // through our miniheaps once more (in case there were frees)
    // for (_attachedOff = 0; _attachedOff < origOff; _attachedOff++) {
    //   MiniHeap *mh = getAttached();
    //   d_assert(mh != nullptr);
    //   if (mh->fullness() > .1) {
    //     attach(arenaBegin, _attachedOff);
    //     return true;
    //   }
    // }

    return false;
  }

  // number of items in the list
  inline size_t ATTRIBUTE_ALWAYS_INLINE length() const {
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
    d_assert(_off < _maxCount);

    auto val = _list[_off];
    _off++;

    return val;
  }

  inline void ATTRIBUTE_ALWAYS_INLINE free(MiniHeap *mh, void *ptr) {
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);
    // const size_t off = (ptrval - _start) / _objectSize;
    // const size_t off = (ptrval - _start) * _objectSizeReciprocal;
    const size_t off = mh->getUnmeshedOff(reinterpret_cast<const void *>(_arenaBegin), ptr);
    // hard_assert_msg(off == off2, "%zu != %zu", off, off2);

    d_assert(off < 256);

    push(sv::Entry{0, static_cast<uint8_t>(off)});
  }

  // an attach takes ownership of the reference to mh
  inline void attach() {
    internal::mwcShuffle(_attachedMiniheaps.array_begin(), _attachedMiniheaps.array_end(), _prng);
    for (size_t i = 0; i < _attachedMiniheaps.size(); i++) {
      const auto mh = _attachedMiniheaps[i];
      _start[i] = mh->getSpanStart(_arenaBegin);

      d_assert(mh->isAttached());

      const auto allocCount = init(mh->writableBitmap());
#ifndef NDEBUG
      if (allocCount == 0) {
        mh->dumpDebug();
      }
#endif
      d_assert_msg(allocCount > 0, "no free bits in MH %p", mh->getSpanStart(_arenaBegin));
    }
  }

  inline void *ATTRIBUTE_ALWAYS_INLINE ptrFromOffset(sv::Entry off) const {
    d_assert(off.miniheapOffset() == 0);
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
    _maxCount = max(HL::CPUInfo::PageSize / sz, kMinStringLen);
    // initially, we are unattached and therefor have no capacity.
    // Setting _off to _maxCount causes isExhausted() to return true
    // so that we don't separately have to check !isAttached() in the
    // malloc fastpath.
    _off = _maxCount;
  }

private:
  uintptr_t _start[kMaxMiniheapsPerShuffleVector];                           // 8
  uint16_t _maxCount{0};                                                     // 2
  uint16_t _off{0};                                                          // 2
  uint32_t _objectSize{0};                                                   // 4
  float _objectSizeReciprocal{0.0};                                          // 4
  uint16_t _attachedOff{0};                                                  //
  const char *_arenaBegin;                                                   // 8
  FixedArray<MiniHeap, kMaxMiniheapsPerShuffleVector> _attachedMiniheaps{};  // 16
  MWC _prng;                                                                 // 36
  // uint8_t __padding[47];                                       // 47  128
  sv::Entry _list[kMaxShuffleVectorLength] CACHELINE_ALIGNED;  // 512 640
};

static_assert(HL::gcd<sizeof(ShuffleVector), CACHELINE_SIZE>::value == CACHELINE_SIZE,
              "ShuffleVector not multiple of cacheline size!");
static_assert(sizeof(ShuffleVector) == 640, "ShuffleVector not expected size!");
}  // namespace mesh

#endif  // MESH__SHUFFLE_VECTOR_H
