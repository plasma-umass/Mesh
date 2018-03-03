// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
/**
 * @file   bitmap.h
 * @brief  A bitmap class, with one bit per element.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */

#pragma once
#ifndef MESH__BITMAP_H
#define MESH__BITMAP_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "static/staticlog.h"

#include "heaplayers.h"

namespace mesh {

using std::atomic_size_t;

template <typename Container, typename size_t>
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
    return _cont.bitmap() == rhs._cont.bitmap() && _i == rhs._i;
  }
  bool operator!=(const BitmapIter &rhs) const {
    return _cont.bitmap() != rhs._cont.bitmap() || _i != rhs._i;
  }
  size_t &operator*() {
    return _i;
  }

private:
  size_t _i;
  const Container &_cont;
};

/**
 * @class Bitmap
 * @brief Manages a dynamically-sized bitmap.
 * @param Heap  the source of memory for the bitmap.
 */

template <typename Heap>
class Bitmap : private Heap {
private:
  DISALLOW_COPY_AND_ASSIGN(Bitmap);

  static_assert(sizeof(size_t) == sizeof(atomic_size_t), "no overhead atomics");

  /// A synonym for the datatype corresponding to a word.
  enum { WORDBITS = sizeof(size_t) * 8 };
  enum { WORDBYTES = sizeof(size_t) };
  /// The log of the number of bits in a size_t, for shifting.
  enum { WORDBITSHIFT = staticlog(WORDBITS) };

  explicit Bitmap() {
  }

public:
  template <typename T>
  using InternalAllocator = STLAllocator<T, Heap>;

  typedef std::basic_string<char, std::char_traits<char>, InternalAllocator<char>> internal_string;

  typedef BitmapIter<Bitmap, size_t> iterator;
  typedef BitmapIter<Bitmap, size_t> const const_iterator;

  explicit Bitmap(size_t nBits) : Bitmap() {
    reserve(nBits);
  }

  explicit Bitmap(const std::string &str) : Bitmap() {
    reserve(str.length());

    for (size_t i = 0; i < str.length(); ++i) {
      char c = str[i];
      d_assert_msg(c == '0' || c == '1', "expected 0 or 1 in bitstring, not %c ('%s')", c, str.c_str());
      if (c == '1')
        tryToSet(i);
    }
  }

  explicit Bitmap(const internal_string &str) : Bitmap() {
    reserve(str.length());

    for (size_t i = 0; i < str.length(); ++i) {
      char c = str[i];
      d_assert_msg(c == '0' || c == '1', "expected 0 or 1 in bitstring, not %c ('%s')", c, str.c_str());
      if (c == '1')
        tryToSet(i);
    }
  }

  Bitmap(Bitmap &&rhs) {
    _elements = rhs._elements;
    _bitarray = rhs._bitarray;
    rhs._bitarray = nullptr;
  }

  ~Bitmap() {
    if (_bitarray)
      Heap::free(_bitarray);
    _bitarray = nullptr;
  }

  internal_string to_string(ssize_t nElements = -1) const {
    if (nElements == -1)
      nElements = _elements;
    d_assert(0 <= nElements && static_cast<size_t>(nElements) <= _elements);

    internal_string s(nElements, '0');

    for (ssize_t i = 0; i < nElements; i++) {
      if (isSet(i))
        s[i] = '1';
    }

    return s;
  }

  /// @brief Sets aside space for a certain number of elements.
  /// @param nelts the number of elements needed.
  void reserve(uint64_t nelts) {
    if (_bitarray) {
      Heap::free(_bitarray);
    }
    // Round up the number of elements.
    _elements = nelts;
    // mesh::debug("Bitmap(%zu): %zu bytes", nelts, byteCount());

    // Allocate the right number of bytes.
    _bitarray = reinterpret_cast<atomic_size_t *>(Heap::malloc(byteCount()));
    d_assert(_bitarray != nullptr);

    clear();
  }

  // number of bytes used to store the bitmap
  inline size_t byteCount() const {
    return WORDBITS * ((_elements + WORDBITS - 1) / WORDBITS) / 8;
  }

  inline size_t bitCount() const {
    return _elements;
  }

  const atomic_size_t *bitmap() const {
    return _bitarray;
  }

  /// Clears out the bitmap array.
  void clear(void) {
    if (_bitarray != nullptr) {
      const auto wordCount = byteCount()/sizeof(size_t);
      // use an explicit array since these are atomic_size_t's
      for (size_t i = 0; i < wordCount; i++) {
        _bitarray[i] = 0;
      }
    }
  }

  inline uint64_t setFirstEmpty(uint64_t startingAt = 0) {
    uint32_t startWord, off;
    computeItemPosition(startingAt, startWord, off);

    const size_t words = byteCount();
    for (size_t i = startWord; i < words; i++) {
      const size_t bits = _bitarray[i];
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
      const bool ok = tryToSetAt(i, off);
      // if we couldn't set the bit, we raced with a different thread.  try again.
      if (!ok) {
        off++;
        continue;
      }

      return WORDBITS * i + off;
    }

    debug("mesh: bitmap completely full, aborting.\n");
    abort();
  }

  /// @return true iff the bit was not set (but it is now).
  inline bool tryToSet(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    return tryToSetAt(item, position);
  }

  /// @return true iff the bit was not set (but it is now).
  inline bool tryToSetRelaxed(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    return tryToSetAtRelaxed(item, position);
  }

  /// Clears the bit at the given index.
  inline bool unset(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);

    auto atomic_bitarray = reinterpret_cast<atomic_size_t *>(_bitarray);

    const auto mask = getMask(position);
    size_t oldValue = atomic_bitarray[item];
    while (!std::atomic_compare_exchange_weak(&atomic_bitarray[item],  // address of word
                                              &oldValue,               // old val
                                              oldValue & ~mask)) {
    }

    return !(oldValue & mask);
  }

  /// Clears the bit at the given index.
  inline bool unsetRelaxed(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);

    const auto mask = getMask(position);
    size_t oldValue = _bitarray[item];
    _bitarray[item] = oldValue & ~mask;

    return !(oldValue & mask);
  }

  // FIXME: who uses this? bad idea with atomics
  inline bool isSet(uint64_t index) const {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    return _bitarray[item] & getMask(position);
  }

  inline uint64_t inUseCount() const {
    const auto wordCount = byteCount() / 8;
    uint64_t count = 0;
    for (size_t i = 0; i < wordCount; i++) {
      count += __builtin_popcountl(_bitarray[i]);
    }
    return count;
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

    const size_t nBytes = byteCount();
    for (size_t i = startWord; i < nBytes; i++) {
      const auto mask = ~((1UL << startOff) - 1);
      const auto bits = _bitarray[i] & mask;
      startOff = 0;

      if (bits == 0ULL)
        continue;

      const size_t off = __builtin_ffsl(bits) - 1;

      const auto bit = WORDBITS * i + off;
      return bit < bitCount() ? bit : bitCount();
    }

    return bitCount();
  }

private:
  inline bool tryToSetAt(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);

    auto atomic_bitarray = reinterpret_cast<atomic_size_t *>(_bitarray);

    size_t oldValue = atomic_bitarray[item];
    while (!std::atomic_compare_exchange_weak(&atomic_bitarray[item],  // address of word
                                              &oldValue,               // old val
                                              oldValue | mask)) {
    }

    return !(oldValue & mask);
  }

  inline bool tryToSetAtRelaxed(uint32_t item, uint32_t position) {
    const auto mask = getMask(position);
    size_t oldValue = _bitarray[item];

    _bitarray[item] = oldValue | mask;

    return !(oldValue & mask);
  }

  /// Given an index, compute its item (word) and position within the word.
  inline void computeItemPosition(uint64_t index, uint32_t &item, uint32_t &position) const {
    d_assert(index < _elements);
    item = index >> WORDBITSHIFT;
    position = index & (WORDBITS - 1);
    d_assert(position == index - (item << WORDBITSHIFT));
    d_assert(item < byteCount() / 8);
  }

  /// To find the bit in a word, do this: word & getMask(bitPosition)
  /// @return a "mask" for the given position.
  inline static size_t getMask(uint64_t pos) {
    return 1UL << pos;
  }

  /// The bit array itself.
  atomic_size_t *_bitarray{nullptr};

  /// The number of elements (bits) in the array.
  size_t _elements{0};
};
}  // namespace mesh

#endif  // MESH__BITMAP_H
