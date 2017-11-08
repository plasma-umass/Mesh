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
// - track _highWaterMark

namespace mesh {

template <typename MiniHeap, size_t BinCount = 4>
class BinnedTracker {
private:
  DISALLOW_COPY_AND_ASSIGN(BinnedTracker);

public:
  BinnedTracker() : _prng(internal::seed()) {
  }

  size_t objectCount() const {
    if (unlikely(!_hasMetadata))
      mesh::debug("BinnedTracker.objectCount() called before set");
    return _objectCount;
  }

  size_t objectSize() const {
    if (unlikely(!_hasMetadata))
      mesh::debug("BinnedTracker.objectSize() called before set");
    return _objectSize;
  }

  internal::vector<MiniHeap *> meshingCandidates(double occupancyCutoff) const {
    std::lock_guard<std::mutex> lock(_mutex);

    internal::vector<MiniHeap *> bucket;

    // consider all of our partially filled miniheaps
    for (size_t i = 0; i < BinCount; i++) {
      const auto partial = _partial[i];
      for (size_t j = 0; j < partial.size(); j++) {
        const auto mh = partial[j];
        if (!mh->isMeshingCandidate() || mh->fullness() >= occupancyCutoff)
          continue;
        bucket.push_back(mh);
      }
    }

    return bucket;
  }

  uint32_t getBinId(uint32_t inUseCount) const {
    if (inUseCount == _objectCount) {
      return internal::BinToken::FlagFull;
    } else if (inUseCount == 0) {
      return internal::BinToken::FlagEmpty;
    } else {
      return inUseCount / BinCount;
    }
  }

  internal::vector<MiniHeap *> &getBin(const uint32_t bin) {
    if (bin == internal::BinToken::FlagFull) {
      return _full;
    } else if (bin == internal::BinToken::FlagEmpty) {
      return _empty;
    } else {
      return _partial[bin];
    }
  }

  // called after a free through the global heap has happened
  void postFree(MiniHeap *mh) {
    const auto inUseCount = mh->inUseCount();
    if (unlikely(inUseCount == _objectCount))
      return;

    // FIXME: does this matter
    if (mh->isAttached())
      return;

    // FIXME: this is maybe more heavyweight than necessary
    std::lock_guard<std::mutex> lock(_mutex);

    // different states: transition from full to not full

    const auto oldBinId = mh->getBinToken().bin();
    const auto newBinId = getBinId(inUseCount);
    if (likely(newBinId == oldBinId))
      return;

    move(getBin(newBinId), getBin(oldBinId), mh, newBinId);
  }

  void add(MiniHeap *mh) {
    std::lock_guard<std::mutex> lock(_mutex);

    d_assert(mh != nullptr);

    if (unlikely(!_hasMetadata))
      setMetadata(mh);

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
  void setMetadata(MiniHeap *mh) {
    _objectCount = mh->maxCount();
    _objectSize = mh->objectSize();
    _hasMetadata = true;
  }

  void swapTokens(MiniHeap *mh1, MiniHeap *mh2) {
    const auto temp = mh1->getBinToken();
    mh1->setBinToken(mh2->getBinToken());
    mh2->setBinToken(temp);
  }

  void move(internal::vector<MiniHeap *> &to, internal::vector<MiniHeap *> &from, MiniHeap *mh, uint32_t size) {
    removeFrom(from, mh);
    mh->setBinToken(internal::BinToken(size, internal::BinToken::FlagNoOff));
    addTo(to, mh);
  }

  // must be called with _mutex held
  void addTo(internal::vector<MiniHeap *> &vec, MiniHeap *mh) {
    const size_t endOff = vec.size();

    mh->setBinToken(mh->getBinToken().newOff(endOff));
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

    vec[endOff]->setBinToken(mh->getBinToken());

    // move our miniheap to the last element, then pop that last element
    std::swap(vec[off], vec[endOff]);

    vec[endOff] = nullptr;
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

  bool _hasMetadata{false};
};
}  // namespace mesh

#endif  // MESH__BINNEDTRACKER_H
