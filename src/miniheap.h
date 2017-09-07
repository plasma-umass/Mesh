// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Copyright 2017 University of Massachusetts, Amherst

#pragma once
#ifndef MESH__MINIHEAP_H
#define MESH__MINIHEAP_H

#include <atomic>
#include <random>

#include "bitmap.h"
#include "freelist.h"
#include "internal.h"

#include "rng/mwc.h"

#include "heaplayers.h"

namespace mesh {

template <size_t MaxFreelistLen = sizeof(uint8_t) << 8,  // AKA max # of objects per miniheap
          size_t MaxMeshes = internal::MaxMeshes>        // maximum number of VM spans we can track
class MiniHeapBase {
private:
  DISALLOW_COPY_AND_ASSIGN(MiniHeapBase);
  typedef MiniHeapBase<MaxFreelistLen, MaxMeshes> MiniHeap;

public:
  MiniHeapBase(void *span, size_t objectCount, size_t objectSize, mt19937_64 &prng, size_t expectedSpanSize)
      : _span{reinterpret_cast<char *>(span)},
        _freelist{objectCount, prng},
        _objectSize(objectSize),
        _meshCount(1),
        _attached(true),
        _bitmap(maxCount()) {
    if (!_span[0])
      abort();

    d_assert_msg(expectedSpanSize == spanSize(), "span size %zu == %zu (%u, %u)", expectedSpanSize, spanSize(),
                 maxCount(), _objectSize);

    // d_assert_msg(spanSize == static_cast<size_t>(_spanSize), "%zu != %hu", spanSize, _spanSize);
    d_assert_msg(objectSize == static_cast<size_t>(_objectSize), "%zu != %hu", objectSize, _objectSize);

    d_assert(_span[1] == nullptr);

    // dumpDebug();
  }

  ~MiniHeapBase() {
    // if (_meshCount > 1)
    //   dumpDebug();
  }

  void markFree() {
    //d_assert(!_attached);
    _freed = true;
  }

  inline void assertNotFreed() const {
    d_assert_msg(!_freed, "MiniHeap(%p:%5zu): Use After Free!\n", this, _objectSize);
  }

  void printOccupancy() const {
    mesh::debug("{\"name\": \"%p\", \"object-size\": %d, \"length\": %d, \"bitmap\": \"%s\"}\n", this, objectSize(),
                maxCount(), _bitmap.to_string().c_str());
  }

  inline void *malloc(size_t sz) {
    assertNotFreed();

    d_assert_msg(_attached && !isExhausted(), "attached: %d, full: %d", _attached, isExhausted());
    d_assert_msg(sz == _objectSize, "sz: %zu _objectSize: %zu", sz, _objectSize);

    auto off = _freelist.pop();
    // mesh::debug("%p: ma %u", this, off);

    auto ptr = mallocAt(off);
    d_assert(ptr != nullptr);

    return ptr;
  }

  inline void localFree(void *ptr, mt19937_64 &prng, MWC &mwc) {
    assertNotFreed();

    const ssize_t freedOff = getOff(ptr);
    if (freedOff < 0)
      return;

    _freelist.push(freedOff, prng, mwc);
    _bitmap.unset(freedOff);
    _inUseCount--;
  }

  inline void free(void *ptr) {
    assertNotFreed();

    const size_t off = getOff(ptr);
    if (off < 0)
      return;

    _bitmap.unset(off);
    _inUseCount--;
  }

  /// Copies (for meshing) the contents of src into our span.
  inline void consume(const MiniHeap *src) {
    assertNotFreed();

    const auto srcSpan = src->getSpanStart();

    // this would be bad
    d_assert(src != this);

    // for each object in src, copy it to our backing span + update
    // our bitmap and in-use count
    for (auto const &off : src->bitmap()) {
      d_assert(!_bitmap.isSet(off));
      void *srcObject = reinterpret_cast<void *>(srcSpan + off * objectSize());
      void *dstObject = mallocAt(off);
      d_assert(dstObject != nullptr);
      memcpy(dstObject, srcObject, objectSize());
    }

    const auto srcSpans = src->spans();
    const auto srcMeshCount = src->meshCount();
    for (size_t i = 0; i < srcMeshCount; i++) {
      trackMeshedSpan(reinterpret_cast<uintptr_t>(srcSpans[i]));
    }
  }

  inline bool contains(void *ptr) const {
    assertNotFreed();

    return spanStart(ptr) != 0;
  }

  inline size_t spanSize() const {
    assertNotFreed();

    size_t bytesNeeded = static_cast<size_t>(_objectSize) * maxCount();
    return mesh::RoundUpToPage(bytesNeeded);
  }

  inline size_t maxCount() const {
    assertNotFreed();

    return _freelist.maxCount();
  }

  inline size_t objectSize() const {
    assertNotFreed();

    return _objectSize;
  }

  inline size_t getSize(void *ptr) const {
    assertNotFreed();

    d_assert_msg(contains(ptr), "span(%p) <= %p < %p", _span[0], ptr,
                 reinterpret_cast<uintptr_t>(_span[0]) + spanSize());

    return objectSize();
  }

  /// Whether we can still locally allocate out of this MiniHeap, or
  /// if our freelist has been exhausted.  This is NOT necessarily the
  /// same as whether our bitmap is full -- if an object is allocated
  /// on one thread, passed to another, and a non-local free happens
  /// from that other thread, we might end up with the case that our
  /// bitmap is non-full, but the freelist is empty (as non-local
  /// frees don't touch the bitmap).
  inline bool isExhausted() const {
    assertNotFreed();

    return _freelist.isExhausted();
  }

  inline uintptr_t getSpanStart() const {
    assertNotFreed();

    return reinterpret_cast<uintptr_t>(_span[0]);
  }

  /// called when a LocalHeap is done with a MiniHeap (it is
  /// "detaching" it and releasing it back to the global heap)
  inline void detach() {
    assertNotFreed();

    _freelist.detach();
    _attached = false;
  }

  inline bool isAttached() const {
    assertNotFreed();

    return _attached;
  }

  inline bool isEmpty() const {
    assertNotFreed();

    return _inUseCount == 0;
  }

  inline size_t inUseCount() const {
    assertNotFreed();

    return _inUseCount;
  }

  inline void ref() const {
    assertNotFreed();

    ++_refCount;
  }

  inline void unref() const {
    assertNotFreed();

    --_refCount;
  }

  inline bool isMeshingCandidate() const {
    assertNotFreed();

    if (_refCount > 0)
      mesh::debug("skipping due to MH reference");
    return !isAttached() && _refCount == 0;
  }

  /// Returns the fraction full (in the range [0, 1]) that this miniheap is.
  inline double fullness() const {
    assertNotFreed();

    return static_cast<double>(_inUseCount) / static_cast<double>(maxCount());
  }

  const mesh::internal::Bitmap &bitmap() const {
    assertNotFreed();

    return _bitmap;
  }

  void trackMeshedSpan(uintptr_t spanStart) {
    assertNotFreed();

    if (_meshCount >= MaxMeshes) {
      mesh::debug("fatal: too many meshes for one miniheap");
      dumpDebug();
      abort();
    }

    _span[_meshCount] = reinterpret_cast<char *>(spanStart);
    _meshCount++;
  }

  size_t meshCount() const {
    assertNotFreed();

    return _meshCount;
  }

  char *const *spans() const {
    assertNotFreed();

    return _span;
  }

  /// Insert the given MiniHeap into our embedded linked list in the 'next' position.
  void insertNext(MiniHeap *mh) {
    assertNotFreed();

    auto nextNext = _next;
    mh->_next = nextNext;
    mh->_prev = this;
    _next = mh;
    if (nextNext)
      nextNext->_prev = mh;
  }

  MiniHeap *next() const {
    assertNotFreed();

    return _next;
  }

  /// Remove this MiniHeap from the list of all miniheaps of this size class
  MiniHeap *remove() {
    assertNotFreed();

    if (_prev != nullptr)
      _prev->_next = _next;

    if (_next != nullptr)
      _next->_prev = _prev;

    return _next;
  }

  /// public for meshTest only
  inline void *mallocAt(size_t off) {
    assertNotFreed();

    if (!_bitmap.tryToSet(off)) {
      mesh::debug("%p: MA %u", this, off);
      dumpDebug();
      return nullptr;
    }

    _inUseCount++;

    return reinterpret_cast<void *>(getSpanStart() + off * _objectSize);
  }

protected:
  inline uintptr_t spanStart(void *ptr) const {
    assertNotFreed();

    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    for (size_t i = 0; i < _meshCount; ++i) {
      if (_span[i] == nullptr) {
        mesh::debug("_span[%d] should be non-null", i);
        dumpDebug();
        d_assert(false);
      }
      const auto span = reinterpret_cast<uintptr_t>(_span[i]);
      if (span <= ptrval && ptrval < span + spanSize())
        return span;
    }

    return 0;
  }

  void dumpDebug() const {
    assertNotFreed();

    const auto heapPages = spanSize() / HL::CPUInfo::PageSize;
    const size_t inUseCount = _inUseCount;
    mesh::debug("MiniHeap(%p:%5zu): %3zu objects on %2zu pages (full: %d, inUse: %zu, mesh: %zu)\t%p-%p\n", this, _objectSize,
                maxCount(), heapPages, this->isExhausted(), inUseCount, _meshCount, _span[0],
                reinterpret_cast<uintptr_t>(_span[0]) + spanSize());
    mesh::debug("\t%s\n", _bitmap.to_string().c_str());
  }

  inline ssize_t getOff(void *ptr) const {
    assertNotFreed();

    d_assert(getSize(ptr) == _objectSize);

    const auto span = spanStart(ptr);
    d_assert(span != 0);
    const auto ptrval = reinterpret_cast<uintptr_t>(ptr);

    const auto off = (ptrval - span) / _objectSize;
    if (span > ptrval || off >= maxCount()) {
      mesh::debug("MiniHeap(%p): invalid free of %p", this, ptr);
      return -1;
    }

    if (unlikely(!_bitmap.isSet(off))) {
      mesh::debug("MiniHeap(%p): double free of %p", this, ptr);
      return -1;
    }

    return off;
  }

  char *_span[MaxMeshes];
  Freelist<MaxFreelistLen> _freelist;
  const uint16_t _objectSize;
  atomic_uint16_t _inUseCount{0};
  mutable atomic_uint16_t _refCount{0};
  uint8_t _meshCount : 7;
  uint8_t _attached : 1;
  mesh::internal::Bitmap _bitmap;
  MiniHeap *_prev{nullptr};
  MiniHeap *_next{nullptr};
  bool _freed{false};
};

typedef MiniHeapBase<> MiniHeap;

static_assert(sizeof(mesh::internal::Bitmap) == 16, "Bitmap too big!");
// static_assert(sizeof(MiniHeap) <= 64, "MiniHeap too big!");

}  // namespace mesh

#endif  // MESH__MINIHEAP_H
