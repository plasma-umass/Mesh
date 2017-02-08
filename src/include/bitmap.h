// -*- C++ -*-

/**
 * @file   bitmap.h
 * @brief  A bitmap class, with one bit per element.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */

#ifndef DH_BITMAP_H
#define DH_BITMAP_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "common.h"
#include "staticlog.h"

/**
 * @class Bitmap
 * @brief Manages a dynamically-sized bitmap.
 * @param Heap  the source of memory for the bitmap.
 */

template <class Heap>
class Bitmap : private Heap {
private:
  /// A synonym for the datatype corresponding to a word.
  typedef size_t word_t;
  enum { WORDBITS = sizeof(word_t) * 8 };
  enum { WORDBYTES = sizeof(word_t) };
  /// The log of the number of bits in a word_t, for shifting.
  enum { WORDBITSHIFT = staticlog(WORDBITS) };

public:
  Bitmap(void) {
  }

  /// @brief Sets aside space for a certain number of elements.
  /// @param nelts the number of elements needed.
  void reserve(uint64_t nelts) {
    if (_bitarray) {
      Heap::free(_bitarray);
    }
    // Round up the number of elements.
    _elements = WORDBITS * ((nelts + WORDBITS - 1) / WORDBITS);
    // Allocate the right number of bytes.
    _bitarray = reinterpret_cast<word_t *>(Heap::malloc(wordCount()));
    d_assert(_bitarray != nullptr);

    clear();
  }

  // number of machine words (4-byte on 32-bit systems, 8-byte on
  // 64-bit) used to store the bitmap
  inline size_t wordCount() const {
    return _elements / 8;
  }

  const word_t *bitmap() const {
    return _bitarray;
  }

  /// Clears out the bitmap array.
  void clear(void) {
    if (_bitarray != nullptr) {
      memset(_bitarray, 0, wordCount());  // 0 = false
    }
  }

  /// @return true iff the bit was not set (but it is now).
  inline bool tryToSet(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    const word_t mask = getMask(position);
    word_t oldvalue = _bitarray[item];
    _bitarray[item] |= mask;
    return !(oldvalue & mask);
  }

  /// Clears the bit at the given index.
  inline bool reset(uint64_t index) {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    word_t oldvalue = _bitarray[item];
    word_t newvalue = oldvalue & ~(getMask(position));
    _bitarray[item] = newvalue;
    return (oldvalue != newvalue);
  }

  inline bool isSet(uint64_t index) const {
    uint32_t item, position;
    computeItemPosition(index, item, position);
    bool result = _bitarray[item] & getMask(position);
    return result;
  }

  inline uint64_t inUseCount() const {
    uint64_t count = 0;
    for (size_t i = 0; i < _elements / WORDBITS; i++) {
      count += __builtin_popcountl(_bitarray[i]);
    }
    return count;
  }

private:
  /// Given an index, compute its item (word) and position within the word.
  void computeItemPosition(uint64_t index, uint32_t &item, uint32_t &position) const {
    assert(index < _elements);
    item = index >> WORDBITSHIFT;
    position = index & (WORDBITS - 1);
    assert(position == index - (item << WORDBITSHIFT));
    assert(item < _elements / WORDBYTES);
  }

  /// To find the bit in a word, do this: word & getMask(bitPosition)
  /// @return a "mask" for the given position.
  inline static word_t getMask(uint64_t pos) {
    return ((word_t)1) << pos;
  }

  /// The bit array itself.
  word_t *_bitarray{nullptr};

  /// The number of elements (bits) in the array.
  size_t _elements{0};
};

#endif
