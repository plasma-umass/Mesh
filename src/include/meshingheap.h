// -*- mode: c++ -*-
// Copyright 2016 University of Massachusetts, Amherst

#ifndef MESH_MESHINGHEAP_H
#define MESH_MESHINGHEAP_H

#include "heaplayers.h"
#include "miniheap.h"

using namespace HL;

template <typename SuperHeap, typename InternalAlloc>
class MeshingHeap {
public:
  enum { Alignment = 16 };  // FIXME

  MeshingHeap() : _current(nullptr), _alloc() {
  }

  inline void *malloc(size_t sz) {
    static int inMalloc = 0;
    // guard against recursive malloc
    if (inMalloc != 0) {
      debug("recursive malloc detected\n");
      abort();
    }
    inMalloc = 1;

    if (unlikely(_current == nullptr)) {
      void *buf = _alloc.malloc(sizeof(MiniHeap<SuperHeap, InternalAlloc>));
      if (!buf)
        abort();
      _current = new (buf) MiniHeap<SuperHeap, InternalAlloc>(sz);
      assert(!_current->isFull());
    }

    void *ptr = _current->malloc(sz);
    if (_current->isFull()) {
      _current = nullptr;
    }

    inMalloc = 0;
    return ptr;
  }

  inline void free(void *ptr) {
    // FIXME: check if ptr is in current, if so free there, otherwise check all other miniheaps
    // this needs to take into account threads + locks, maybe
  }

  inline size_t getSize(void *ptr) {
    if (_current == nullptr)
      return 0;
    return _current->getSize(ptr);
  }

private:
  MiniHeap<SuperHeap, InternalAlloc> *_current;
  InternalAlloc _alloc;
  // TODO: btree of address-ranges to miniheaps, for free
};

#endif  // MESH_MESHINGHEAP_H
