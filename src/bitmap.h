// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_BITMAP_H
#define MESH_BITMAP_H

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "internal.h"

#include "static/log.h"

#include "heaplayers.h"

namespace mesh {
namespace bitmap {

static constexpr size_t kWordBits = sizeof(size_t) * 8;
static constexpr size_t kWordBytes = sizeof(size_t);
/// The log of the number of bits in a size_t, for shifting.
static constexpr size_t kWordBitshift = staticlog(kWordBits);
// number of bytes used to store the bitmap -- rounds up to nearest sizeof(size_t)
static inline constexpr size_t ATTRIBUTE_ALWAYS_INLINE representationSize(size_t bitCount) {
  return kWordBits * ((bitCount + kWordBits - 1) / kWordBits) / 8;
}
static inline constexpr size_t ATTRIBUTE_ALWAYS_INLINE wordCount(size_t byteCount) {
  return byteCount / kWordBytes;
}
/// To find the bit in a word, do this: word & getMask(bitPosition)
/// @return a "mask" for the given position.
static inline constexpr size_t ATTRIBUTE_ALWAYS_INLINE getMask(uint64_t pos) {
  return 1UL << pos;
}

using std::atomic_compare_exchange_weak_explicit;
using std::atomic_size_t;

// enables iteration through the set bits of the bitmap
template <typename Container>
class BitmapIter : public std::iterator<std::forward_iterator_tag, size_t> {
public:
  BitmapIter(const Container &a, const size_t i) : _i(i), _cont(a) {
  }
  BitmapIter &operator++() {
    if (unlikely(_i + 1 >= _cont.bitCount())) {
      _i = _cont.bitCount();
      return *this;
    }

    _i = _cont.lowestSetBitAt(_i + 1);
    return *this;
  }
  bool operator==(const BitmapIter &rhs) const {
    return _cont.bits() == rhs._cont.bits() && _i == rhs._i;
  }
  bool operator!=(const BitmapIter &rhs) const {
    return _cont.bits() != rhs._cont.bits() || _i != rhs._i;
  }
  size_t &operator*() {
    return _i;
  }

private:
  size_t _i;
  const Container &_cont;
};

template <size_t maxBits>
class AtomicBitmapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(AtomicBitmapBase);

public:
  typedef atomic_size_t word_t;

  enum { MaxBitCount = maxBits };

protected:
  AtomicBitmapBase(size_t bitCount) {
    d_assert_msg(bitCount <= maxBits, "max bits (%zu) exceeded: %zu", maxBits, bitCount);

    static_assert(wordCount(representationSize(maxBits)) == 4, "unexpected representation size");
    // for (size_t i = 0; i < wordCount(representationSize(maxBits)); i++) {
    //   _bits[i].store(0, std::memory_order_relaxed);
    // }
    _bits[0].store(0, std::memory_order_relaxed);
    _bits[1].store(0, std::memory_order_relaxed);
    _bits[2].store(0, std::memory_order_relaxed);
    _bits[3].store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
  }

  ~AtomicBitmapBase() {
  }

  inline void ATTRIBUTE_ALWAYS_INLINE setAndExchangeAll(size_t *oldBits, const size_t *newBits) {
    // for (size_t i = 0; i < wordCount(representationSize(maxBits)); i++) {
    //   oldBits[i] = _bits[i].exchange(newBits[i]);
    // }
    oldBits[0] = _bits[0].exchange(newBits[0], std::memory_order_acq_rel);
    oldBits[1] = _bits[1].exchange(newBits[1], std::memory_order_acq_rel);
    oldBits[2] = _bits[2].exchange(newBits[2], std::memory_order_acq_rel);
    oldBits[3] = _bits[3].exchange(newBits[3], std::memory_order_acq_rel);
  }

public:
  inline bool ATTRIBUTE_ALWAYS_INLINE setAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item].load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_bits[item],               // address of word
                                                  &oldValue,                  // old val
                                                  oldValue | mask,            // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }

    return !(oldValue & mask);
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE unsetAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item].load(std::memory_order_relaxed);
    while (!atomic_compare_exchange_weak_explicit(&_bits[item],               // address of word
                                                  &oldValue,                  // old val
                                                  oldValue & ~mask,           // new val
                                                  std::memory_order_release,  // success mem model
                                                  std::memory_order_relaxed)) {
    }

    return !(oldValue & mask);
  }

  inline uint32_t ATTRIBUTE_ALWAYS_INLINE inUseCount() const {
    return __builtin_popcountl(_bits[0]) + __builtin_popcountl(_bits[1]) + __builtin_popcountl(_bits[2]) +
           __builtin_popcountl(_bits[3]);
  }

protected:
  inline void nullBits() {
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE bitCount() const {
    return maxBits;
  }

  word_t _bits[wordCount(representationSize(maxBits))] = {};
};

class RelaxedBitmapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(RelaxedBitmapBase);

public:
  typedef size_t word_t;

  enum { MaxBitCount = std::numeric_limits<uint64_t>::max() };

protected:
  RelaxedBitmapBase(size_t bitCount)
      : _bitCount(bitCount),
        _isDynamicallyAllocated(1),
        _bits(reinterpret_cast<word_t *>(internal::Heap().malloc(representationSize(bitCount)))) {
    d_assert(_bits != nullptr);
    clear();
  }

  RelaxedBitmapBase(size_t bitCount, char *backingMemory, bool clear)
      : _bitCount(bitCount), _isDynamicallyAllocated(0), _bits(reinterpret_cast<word_t *>(backingMemory)) {
    d_assert(_bits != nullptr);
    if (clear) {
      this->clear();
    }
  }

  ~RelaxedBitmapBase() {
    if (_isDynamicallyAllocated && _bits) {
      internal::Heap().free(_bits);
    }
    _bits = nullptr;
  }

public:
  inline void invert() {
    const size_t numWords = wordCount(representationSize(_bitCount));
    for (size_t i = 0; i < numWords; i++) {
      _bits[i] = ~_bits[i];
    }
  }

  inline void setAll(uint64_t bitCount) {
    for (size_t i = 0; bitCount > 0; i++) {
      if (bitCount >= 64) {
        _bits[i] = (unsigned long)-1;
        bitCount -= 64;
      } else {
        _bits[i] = (1ULL << bitCount) - 1;
        bitCount = 0;
      }
    }
  }

  inline bool setAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item];
    _bits[item] = oldValue | mask;

    return !(oldValue & mask);
  }

  /// Clears the bit at the given index.
  inline bool unsetAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item];
    _bits[item] = oldValue & ~mask;

    return !(oldValue & mask);
  }

  inline uint32_t inUseCount() const {
    const auto wordCount = representationSize(_bitCount) / sizeof(size_t);
    uint32_t count = 0;
    for (size_t i = 0; i < wordCount; i++) {
      count += __builtin_popcountl(_bits[i]);
    }
    return count;
  }

protected:
  inline void nullBits() {
    _bits = nullptr;
  }

  void clear() {
    memset(_bits, 0, representationSize(bitCount()));
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE bitCount() const {
    return _bitCount;
  }

  const size_t _bitCount : 63;
  const size_t _isDynamicallyAllocated : 1;
  word_t *_bits;
};

template <size_t maxBits>
class RelaxedFixedBitmapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(RelaxedFixedBitmapBase);

public:
  typedef size_t word_t;

  enum { MaxBitCount = maxBits };

protected:
  RelaxedFixedBitmapBase(size_t bitCount) {
    clear();
  }

public:
  inline void ATTRIBUTE_ALWAYS_INLINE invert() {
    // constexpr size_t numWords = wordCount(representationSize(maxBits));
    // for (size_t i = 0; i < numWords; i++) {
    //   _bits[i] = ~_bits[i];
    // }
    _bits[0] = ~_bits[0];
    _bits[1] = ~_bits[1];
    _bits[2] = ~_bits[2];
    _bits[3] = ~_bits[3];
  }

  inline void ATTRIBUTE_ALWAYS_INLINE setAll(uint64_t bitCount) {
    for (size_t i = 0; bitCount > 0; i++) {
      if (bitCount >= 64) {
        _bits[i] = (unsigned long)-1;
        bitCount -= 64;
      } else {
        _bits[i] = (1ULL << bitCount) - 1;
        bitCount = 0;
      }
    }
  }

  inline bool ATTRIBUTE_ALWAYS_INLINE setAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item];
    _bits[item] = oldValue | mask;

    return !(oldValue & mask);
  }

  /// Clears the bit at the given index.
  inline bool ATTRIBUTE_ALWAYS_INLINE unsetAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    size_t oldValue = _bits[item];
    _bits[item] = oldValue & ~mask;

    return !(oldValue & mask);
  }

  inline uint64_t ATTRIBUTE_ALWAYS_INLINE inUseCount() const {
    constexpr auto wordCount = representationSize(maxBits) / sizeof(size_t);
    uint32_t count = 0;
    // for (size_t i = 0; i < wordCount; i++) {
    //   count += __builtin_popcountl(_bits[i]);
    // }
    count += __builtin_popcountl(_bits[0]);
    count += __builtin_popcountl(_bits[1]);
    count += __builtin_popcountl(_bits[2]);
    count += __builtin_popcountl(_bits[3]);
    return count;
  }

protected:
  void ATTRIBUTE_ALWAYS_INLINE clear() {
    _bits[0] = 0;
    _bits[1] = 0;
    _bits[2] = 0;
    _bits[3] = 0;
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE bitCount() const {
    return maxBits;
  }

  word_t _bits[wordCount(representationSize(maxBits))] = {};
};

template <typename Super>
class BitmapBase : public Super {
public:
  typedef typename Super::word_t word_t;

private:
  DISALLOW_COPY_AND_ASSIGN(BitmapBase);

  // typedef AtomicBitmapBase Super;
  // typedef RelaxedBitmapBase Super;
  typedef BitmapBase<Super> Bitmap;

  static_assert(sizeof(size_t) == sizeof(atomic_size_t), "no overhead atomics");
  static_assert(sizeof(word_t) == sizeof(size_t), "word_t should be size_t");

  BitmapBase() = delete;

public:
  typedef BitmapIter<Bitmap> iterator;
  typedef BitmapIter<Bitmap> const const_iterator;

  explicit BitmapBase(size_t bitCount) : Super(bitCount) {
  }

  explicit BitmapBase(size_t bitCount, char *backingMemory, bool clear = true) : Super(bitCount, backingMemory, clear) {
  }

  explicit BitmapBase(const std::string &str) : Super(str.length()) {
    for (size_t i = 0; i < str.length(); ++i) {
      char c = str[i];
      d_assert_msg(c == '0' || c == '1', "expected 0 or 1 in bitstring, not %c ('%s')", c, str.c_str());
      if (c == '1')
        tryToSet(i);
    }
  }

  explicit BitmapBase(const internal::string &str) : Super(str.length()) {
    for (size_t i = 0; i < str.length(); ++i) {
      char c = str[i];
      d_assert_msg(c == '0' || c == '1', "expected 0 or 1 in bitstring, not %c ('%s')", c, str.c_str());
      if (c == '1')
        tryToSet(i);
    }
  }

  BitmapBase(Bitmap &&rhs) : Super(rhs.bitCount()) {
    rhs.Super::nullBits();
  }

  internal::string to_string(ssize_t bitCount = -1) const {
    if (bitCount == -1)
      bitCount = this->bitCount();
    d_assert(0 <= bitCount && static_cast<size_t>(bitCount) <= this->bitCount());

    internal::string s(bitCount + 1, 0);

    for (ssize_t i = 0; i < bitCount; i++) {
      s[i] = isSet(i) ? '1' : '0';
    }

    return s;
  }

  // number of bytes used to store the bitmap -- rounds up to nearest sizeof(size_t)
  inline size_t byteCount() const {
    return representationSize(bitCount());
  }

  inline size_t ATTRIBUTE_ALWAYS_INLINE bitCount() const {
    return Super::bitCount();
  }

  inline uint64_t setFirstEmpty(uint64_t startingAt = 0) {
    uint32_t startWord, off;
    computeItemPosition(startingAt, startWord, off);

    const size_t words = byteCount();
    // const auto words = byteCount() / sizeof(size_t);
    for (size_t i = startWord; i < words; i++) {
      const size_t bits = Super::_bits[i];
      if (bits == ~0UL) {
        off = 0;
        continue;
      }

      d_assert(off <= 63U);
      size_t unsetBits = ~bits;
      d_assert(unsetBits != 0);

      // if the offset is 3, we want to mark the first 3 bits as 'set'
      // or 'unavailable'.
      unsetBits &= ~((1UL << off) - 1);

      // if, after we've masked off everything below our offset there
      // are no free bits, continue
      if (unsetBits == 0) {
        off = 0;
        continue;
      }

      // debug("unset bits: %zx (off: %u, startingAt: %llu", unsetBits, off, startingAt);

      size_t off = __builtin_ffsll(unsetBits) - 1;
      const bool ok = Super::setAt(i, off);
      // if we couldn't set the bit, we raced with a different thread.  try again.
      if (!ok) {
        off++;
        continue;
      }

      return kWordBits * i + off;
    }

    debug("mesh: bitmap completely full, aborting.\n");
    abort();
  }

  /// @return true iff the bit was not set (but it is now).
  inline bool ATTRIBUTE_ALWAYS_INLINE tryToSet(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    return Super::setAt(item, position);
  }

  /// Clears the bit at the given index.
  inline bool ATTRIBUTE_ALWAYS_INLINE unset(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);

    return Super::unsetAt(item, position);
  }

  // FIXME: who uses this? bad idea with atomics
  inline bool ATTRIBUTE_ALWAYS_INLINE isSet(uint64_t index) const {
    uint32_t item, position;
    computeItemPosition(index, item, position);

    return Super::_bits[item] & getMask(position);
  }

  const word_t *bits() const {
    return Super::_bits;
  }

  word_t *mut_bits() {
    return Super::_bits;
  }

  iterator begin() {
    return iterator(*this, lowestSetBitAt(0));
  }
  iterator end() {
    return iterator(*this, bitCount());
  }
  const_iterator begin() const {
    return iterator(*this, lowestSetBitAt(0));
  }
  const_iterator end() const {
    return iterator(*this, bitCount());
  }
  const_iterator cbegin() const {
    return iterator(*this, lowestSetBitAt(0));
  }
  const_iterator cend() const {
    return iterator(*this, bitCount());
  }

  size_t lowestSetBitAt(uint64_t startingAt) const {
    uint32_t startWord, startOff;
    computeItemPosition(startingAt, startWord, startOff);

    const auto wordCount = byteCount() / sizeof(size_t);
    for (size_t i = startWord; i < wordCount; i++) {
      const auto mask = ~((1UL << startOff) - 1);
      const auto bits = Super::_bits[i] & mask;
      startOff = 0;

      if (bits == 0ULL)
        continue;

      const size_t off = __builtin_ffsl(bits) - 1;

      const auto bit = kWordBits * i + off;
      return bit < bitCount() ? bit : bitCount();
    }

    return bitCount();
  }

  size_t highestSetBitBeforeOrAt(uint64_t startingAt) const {
    uint32_t startWord, startOff;
    computeItemPosition(startingAt, startWord, startOff);

    const auto wordCount = byteCount() / sizeof(size_t);
    for (ssize_t i = startWord; i >= 0; i--) {
      uint64_t mask = (1UL << (startOff + 1)) - 1;
      if (startOff == 63) {
        mask = ~0UL;
      }
      const auto bits = Super::_bits[i] & mask;
      const auto origStartOff = startOff;
      startOff = 63;

      if (bits == 0ULL)
        continue;

      const size_t off = 64 - __builtin_clzl(bits) - 1;

      const auto bit = kWordBits * i + off;
      return bit < bitCount() ? bit : bitCount();
    }

    return 0;
  }

  inline void setAndExchangeAll(size_t *oldBits, const size_t *newBits) {
    Super::setAndExchangeAll(oldBits, newBits);
  }

private:
  /// Given an index, compute its item (word) and position within the word.
  inline void ATTRIBUTE_ALWAYS_INLINE computeItemPosition(uint64_t index, uint32_t &item, uint32_t &position) const {
    d_assert(index < bitCount());
    item = index >> kWordBitshift;
    position = index & (kWordBits - 1);
    d_assert(position == index - (item << kWordBitshift));
    d_assert(item < byteCount() / 8);
  }
};
}  // namespace bitmap

namespace internal {
typedef bitmap::BitmapBase<bitmap::AtomicBitmapBase<256>> Bitmap;
typedef bitmap::BitmapBase<bitmap::RelaxedFixedBitmapBase<256>> RelaxedFixedBitmap;
typedef bitmap::BitmapBase<bitmap::RelaxedBitmapBase> RelaxedBitmap;

static_assert(sizeof(Bitmap) == sizeof(size_t) * 4, "Bitmap unexpected size");
static_assert(sizeof(RelaxedFixedBitmap) == sizeof(size_t) * 4, "Bitmap unexpected size");
static_assert(sizeof(RelaxedBitmap) == sizeof(size_t) * 2, "Bitmap unexpected size");
}  // namespace internal
}  // namespace mesh

#endif  // MESH_BITMAP_H
