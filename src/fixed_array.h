// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2019 The Mesh Authors. All rights reserved.
// Use of this source code is governed by the Apache License,
// Version 2.0, that can be found in the LICENSE file.

#pragma once
#ifndef MESH_FIXED_ARRAY_H
#define MESH_FIXED_ARRAY_H

#include <iterator>

namespace mesh {

// enables iteration through the miniheaps in an array
template <typename FixedArray>
class FixedArrayIter : public std::iterator<std::forward_iterator_tag, typename FixedArray::value_type> {
public:
  FixedArrayIter(const FixedArray &a, const uint32_t i) : _i(i), _array(a) {
  }
  FixedArrayIter &operator++() {
    if (unlikely(_i + 1 >= _array.size())) {
      _i = _array.size();
      return *this;
    }

    _i++;
    return *this;
  }
  bool operator==(const FixedArrayIter &rhs) const {
    return &_array == &rhs._array && _i == rhs._i;
  }
  bool operator!=(const FixedArrayIter &rhs) const {
    return &_array != &rhs._array || _i != rhs._i;
  }
  typename FixedArray::value_type operator*() {
    return _array[_i];
  }

private:
  uint32_t _i;
  const FixedArray &_array;
};

template <typename T, uint32_t Capacity>
class FixedArray {
private:
  DISALLOW_COPY_AND_ASSIGN(FixedArray);

public:
  typedef T *value_type;
  typedef FixedArrayIter<FixedArray<T, Capacity>> iterator;
  typedef FixedArrayIter<FixedArray<T, Capacity>> const const_iterator;

  FixedArray() {
    d_assert(_size == 0);
#ifndef NDEBUG
    for (uint32_t i = 0; i < Capacity; i++) {
      d_assert(_objects[i] == nullptr);
    }
#endif
  }

  ~FixedArray() {
    clear();
  }

  uint32_t size() const {
    return _size;
  }

  bool full() const {
    return _size == Capacity;
  }

  void clear() {
    memset(_objects, 0, Capacity * sizeof(T *));
    _size = 0;
  }

  void append(T *obj) {
    hard_assert(_size < Capacity);
    _objects[_size] = obj;
    _size++;
  }

  T *operator[](uint32_t i) const {
    // d_assert(i < _size);
    return _objects[i];
  }

  T **array_begin() {
    return &_objects[0];
  }
  T **array_end() {
    return &_objects[size()];
  }

  iterator begin() {
    return iterator(*this, 0);
  }
  iterator end() {
    return iterator(*this, size());
  }
  const_iterator begin() const {
    return iterator(*this, 0);
  }
  const_iterator end() const {
    return iterator(*this, size());
  }
  const_iterator cbegin() const {
    return iterator(*this, 0);
  }
  const_iterator cend() const {
    return iterator(*this, size());
  }

private:
  T *_objects[Capacity]{};
  uint32_t _size{0};
};

}  // namespace mesh

#endif  // MESH_FIXED_ARRAY_H
