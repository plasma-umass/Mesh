// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

/*

  Heap Layers: An Extensible Memory Allocation Infrastructure

  Copyright (C) 2000-2012 by Emery Berger
  http://www.cs.umass.edu/~emery
  emery@cs.umass.edu

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#pragma once
#ifndef MESH__LOCKED_HEAP_H
#define MESH__LOCKED_HEAP_H

#include <mutex>

namespace mesh {

template <class Super>
class LockedHeap : public Super {
public:
  enum { Alignment = Super::Alignment };

  inline void *malloc(size_t sz) {
    std::lock_guard<std::mutex> lock(_mutex);
    return Super::malloc(sz);
  }

  inline void free(void *ptr) {
    std::lock_guard<std::mutex> lock(_mutex);
    Super::free(ptr);
  }

  inline size_t getSize(void *ptr) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return Super::getSize(ptr);
  }

  inline void lock(void) {
    _mutex.lock();
  }

  inline void unlock(void) {
    _mutex.unlock();
  }

private:
  //    char dummy[128]; // an effort to avoid false sharing.
  mutable std::mutex _mutex;
};
}  // namespace mesh

#endif
