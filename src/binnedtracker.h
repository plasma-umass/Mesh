// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

#pragma once
#ifndef MESH__BINNEDTRACKER_H
#define MESH__BINNEDTRACKER_H

#include "internal.h"

// invariants:
// - MiniHeap only in one one bin in one BinnedTracker

// want:
// - 'side-bins' for full + empty spans
// - $BinCount partially full bins

// TODO:
// - track _highWaterMark
// - integrate into meshing methods
// - on global free, call into here so we can transfer bins

namespace mesh {

template <typename MiniHeap, size_t BinCount>
class BinnedTracker {
private:
  DISALLOW_COPY_AND_ASSIGN(BinnedTracker);

public:
  BinnedTracker() {
  }

  size_t objectCount() const {
    return _objectCount;
  }

  size_t objectSize() const {
    return _objectSize;
  }

  void add(MiniHeap *mh) {
    _full.push_back(mh);
  }

  void remove(MiniHeap *mh) {
    debug("TODO: implement BinnedTracker::remove");
  }

  // number of MiniHeaps we are tracking
  size_t count() const {
    size_t sz = _full.size() + _empty.size();

    for (size_t i = 0; i < BinCount; i++) {
      sz += _partial[i].size();
    }

    return sz;
  }

  void printOccupancy() const {
    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr)
        _full[i]->printOccupancy();
    }

    for (size_t i = 0; i < BinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; i < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr && !mh->isAttached())
          mh->printOccupancy();
      }
    }

    // no need to print occupancy of _empty MiniHeaps
  }

  void dumpStats(bool beDetailed) const {
    const auto mhCount = count();

    if (mhCount == 0) {
      debug("MH HWM (%5zu : %3zu):     %6zu/%6zu\n", _objectSize, 0, 0, _highWaterMark.load());
      return;
    }

    const auto totalObjects = mhCount * _objectCount;

    double inUseCount = 0;

    for (size_t i = 0; i < _full.size(); i++) {
      if (_full[i] != nullptr) {
        inUseCount += _full[i]->inUseCount();
        // if (beDetailed && size == 4096)
        //   debug("\t%5.2f\t%s\n", mh->fullness(), mh->bitmap().to_string().c_str());
      }
    }

    for (size_t i = 0; i < BinCount; i++) {
      auto partial = _partial[i];
      for (size_t j = 0; i < partial.size(); j++) {
        MiniHeap *mh = partial[j];
        if (mh != nullptr)
          inUseCount += mh->inUseCount();
      }
    }

    debug("MH HWM (%5zu : %3zu):     %6zu/%6zu (occ: %f)\n", _objectSize, totalObjects, mhCount, _highWaterMark.load(),
          inUseCount / totalObjects);
  }

private:
  void setMetadata(size_t objectCount, size_t objectSize) {
    _objectCount = objectCount;
    _objectSize = objectSize;
  }

  internal::vector<MiniHeap *> _full;
  internal::vector<MiniHeap *> _partial[BinCount];
  internal::vector<MiniHeap *> _empty;

  size_t _objectSize{0};
  size_t _objectCount{0};

  atomic_size_t _highWaterMark;
};
}  // namespace mesh

#endif  // MESH__BINNEDTRACKER_H
