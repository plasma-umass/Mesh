// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil -*-

#pragma once
#ifndef MESH__BINNEDTRACKER_H
#define MESH__BINNEDTRACKER_H

#include <mutex>

#include "internal.h"

// invariants:
// - MiniHeap only in one one bin in one BinnedTracker

// want:
// - 'side-bins' for full + empty spans
// - $BinCount partially full bins

// TODO:
// - setMetadata
// - track _highWaterMark
// - integrate into meshing methods
// - on global free, call into here so we can transfer bins

namespace mesh {

template <typename MiniHeap, size_t BinCount>
class BinnedTracker {
private:
  DISALLOW_COPY_AND_ASSIGN(BinnedTracker);

public:
  BinnedTracker() : _prng(internal::seed()) {
  }

  size_t objectCount() const {
    return _objectCount;
  }

  size_t objectSize() const {
    return _objectSize;
  }

  void add(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    d_assert(mh != nullptr);

    mh->setBinToken(internal::BinToken::Full());
    addTo(_full, mh);
  }

  void remove(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    if (unlikely(!mh->getBinToken().valid())) {
      mesh::debug("ERROR: bad bin token");
      return;
    }

    const auto bin = mh->getBinToken().bin();
    if (bin == internal::BinToken::FlagFull) {
      removeFrom(_full, mh);
    } else if (bin == internal::BinToken::FlagEmpty) {
      removeFrom(_empty, mh);
    } else {
      if (unlikely(bin >= BinCount)) {
        mesh::debug("ERROR: bin too big: %u", bin);
        return;
      }

      removeFrom(_partial[bin], mh);
    }
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
      debug("MH HWM (%5zu : %3zu):     %6zu/%6zu\n", _objectSize.load(), 0, 0, _highWaterMark.load());
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

    debug("MH HWM (%5zu : %3zu):     %6zu/%6zu (occ: %f)\n", _objectSize.load(), totalObjects, mhCount,
          _highWaterMark.load(), inUseCount / totalObjects);
  }

private:
  void setMetadata(size_t objectCount, size_t objectSize) {
    _objectCount = objectCount;
    _objectSize = objectSize;
  }

  void swapTokens(MiniHeap *mh1, MiniHeap *mh2) {
    const auto temp = mh1->getBinToken();
    mh1->setBinToken(mh2->getBinToken());
    mh2->setBinToken(temp);
  }

  // must be called with _mutex held
  void addTo(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    const size_t endOff = vec.size();
    const auto tok = mh->getBinToken().newOff(endOff);

    mh->setBinToken(tok);
    vec.push_back(mh);

    // endpoint is _inclusive_, so we subtract 1 from size since we're
    // dealing with 0-indexed offsets
    std::uniform_int_distribution<size_t> distribution(0, vec.size() - 1);

    const size_t swapOff = distribution(_prng);

    std::swap(vec[swapOff], vec[endOff]);
    swapTokens(vec[swapOff], vec[endOff]);
  }

  // must be called with _mutex held
  void removeFrom(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    const size_t off = mh->getBinToken().off();
    const size_t endOff = vec.size() - 1;

    // move our miniheap to the last element, then pop that last element
    std::swap(vec[off], vec[endOff]);
    vec.pop_back();

    // update the miniheap's token
    mh->setBinToken(mh->getBinToken().newOff(internal::BinToken::FlagNoOff));
  }

  internal::vector<MiniHeap *> _full;
  internal::vector<MiniHeap *> _partial[BinCount];
  internal::vector<MiniHeap *> _empty;

  atomic_size_t _objectSize{0};
  atomic_size_t _objectCount{0};

  atomic_size_t _highWaterMark{0};

  mt19937_64 _prng;
  mutable std::mutex _mutex{};
};
}  // namespace mesh

#endif  // MESH__BINNEDTRACKER_H
