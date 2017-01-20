// -*- C++ -*-


/**
 * @file   bitmap.h
 * @brief  A bitmap class, with one bit per element.
 * @author Emery Berger <http://www.cs.umass.edu/~emery>
 * @note   Copyright (C) 2005 by Emery Berger, University of Massachusetts Amherst.
 */


#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "staticlog.h"

#ifndef DH_BITMAP_H
#define DH_BITMAP_H

/**
 * @class BitMap
 * @brief Manages a dynamically-sized bitmap.
 * @param Heap  the source of memory for the bitmap.
 */

template <class Heap>
class BitMap : private Heap {
public:

  BitMap (void)
    : _bitarray (NULL),
      _elements (0)
  {
  }

  /**
   * @brief Sets aside space for a certain number of elements.
   * @param  nelts  the number of elements needed.
   */
  
  void reserve (unsigned long long nelts) {
    if (_bitarray) {
      Heap::free (_bitarray);
    }
    // Round up the number of elements.
    _elements = WORDBITS * ((nelts + WORDBITS - 1) / WORDBITS);
    // Allocate the right number of bytes.
    void * buf = Heap::malloc (_elements / 8);
    _bitarray = (WORD *) buf;
    clear();
  }

  /// Clears out the bitmap array.
  void clear (void) {
    if (_bitarray != NULL) {
      memset (_bitarray, 0, _elements / 8); // 0 = false
    }
  }

  /// @return true iff the bit was not set (but it is now).
  inline bool tryToSet (unsigned long long index) {
    unsigned int item, position;
    computeItemPosition (index, item, position);
    const WORD mask = getMask(position);
    unsigned long oldvalue = _bitarray[item];
    _bitarray[item] |= mask;
    return !(oldvalue & mask);
  }

  /// Clears the bit at the given index.
  inline bool reset (unsigned long long index) {
    unsigned int item, position;
    computeItemPosition (index, item, position);
    unsigned long oldvalue = _bitarray[item];
    WORD newvalue = oldvalue &  ~(getMask(position));
    _bitarray[item] = newvalue;
    return (oldvalue != newvalue);
  }

  inline bool isSet (unsigned long long index) const {
    unsigned int item, position;
    computeItemPosition (index, item, position);
    bool result = _bitarray[item] & getMask(position);
    return result;
  }

private:

  /// Given an index, compute its item (word) and position within the word.
  void computeItemPosition (unsigned long long index,
			    unsigned int& item,
			    unsigned int& position) const
  {
    assert (index < _elements);
    item = index >> WORDBITSHIFT;
    position = index & (WORDBITS - 1);
    assert (position == index - (item << WORDBITSHIFT));
    assert (item < _elements / WORDBYTES);
  }

  /// A synonym for the datatype corresponding to a word.
  typedef size_t WORD;

  /// To find the bit in a word, do this: word & getMask(bitPosition)
  /// @return a "mask" for the given position.
  inline static WORD getMask (unsigned long long pos) {
    return ((WORD) 1) << pos;
  }

  /// The number of bits in a WORD.
  enum { WORDBITS = sizeof(WORD) * 8 };

  /// The number of BYTES in a WORD.
  enum { WORDBYTES = sizeof(WORD) };

  /// The log of the number of bits in a WORD, for shifting.
#if __cplusplus > 199711L
  enum { WORDBITSHIFT = staticlog(WORDBITS) };
#else
  enum { WORDBITSHIFT = StaticLog<WORDBITS>::VALUE };
#endif

  /// The bit array itself.
  WORD * _bitarray;
  
  /// The number of elements in the array.
  unsigned long _elements;

};

#endif
